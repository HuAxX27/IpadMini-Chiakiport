// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKLanConsoleService.h"

#include <string.h>

/**
 * Registered consoles that have not answered yet would show as "no contesta"
 * for the moment the first search takes: the list waits this long for it.
 */
static const NSTimeInterval CKFirstSearchSeconds = 2.0;

@interface CKLanConsoleService ()
- (void)browserFound:(NSData *)consoles generation:(NSUInteger)generation;
@end

@implementation CKLanConsoleService
{
	ChiakiLog _log;
	NSString *_registryPath;
	CKRegistry _registry;
	/** NO when the file could not be read: then it is never written over. */
	BOOL _registryReadable;
	CKConsoleBrowser *_browser;
	/** Counts starts and stops, so news from a stopped browser that arrives late is dropped. */
	NSUInteger _generation;
	/** _generation as the browser's thread reads it. */
	volatile NSUInteger _browserGeneration;
	CKConsole _discovered[CK_CONSOLES_MAX];
	size_t _discoveredCount;
	/** The first search had its time to answer. */
	BOOL _searched;
	NSArray<CKConsoleItem *> *_items;
	NSString *_searchProblem;
	NSString *_registryProblem;
	dispatch_queue_t _queue;
}

@synthesize delegate = _delegate;

// The browser's thread: copy the list and hop to the main thread. stop joins
// this thread before the service can go away.
static void browser_cb(const CKConsole *consoles, size_t count, void *user)
{
	CKLanConsoleService *service = (__bridge CKLanConsoleService *)user;
	__weak CKLanConsoleService *weak = service;
	NSUInteger generation = service->_browserGeneration;
	NSData *data = [NSData dataWithBytes:consoles length:count * sizeof(CKConsole)];
	dispatch_async(dispatch_get_main_queue(), ^{
		[weak browserFound:data generation:generation];
	});
}

- (instancetype)init
{
	return [self initWithRegistryPath:@(CK_REGISTRY_DEFAULT_PATH)];
}

- (instancetype)initWithRegistryPath:(NSString *)path
{
	self = [super init];
	if(!self)
		return nil;
	chiaki_log_init(&_log, CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR, chiaki_log_cb_print, NULL);
	_registryPath = [path copy];
	_queue = dispatch_queue_create("chiakiport.consoles", DISPATCH_QUEUE_SERIAL);
	_items = @[];
	[self loadRegistry];
	return self;
}

- (void)dealloc
{
	ck_console_browser_stop(_browser);
}

- (BOOL)isSimulated
{
	return NO;
}

- (NSArray<CKConsoleItem *> *)consoles
{
	return _items;
}

- (NSString *)problem
{
	if(_searchProblem && _registryProblem)
		return [NSString stringWithFormat:@"%@ %@", _searchProblem, _registryProblem];
	return _searchProblem ?: _registryProblem;
}

// ---------------------------------------------------------------------------
// Saved registrations
// ---------------------------------------------------------------------------

- (void)loadRegistry
{
	CKRegistry registry;
	ChiakiErrorCode err = ck_registry_load(&registry, _registryPath.fileSystemRepresentation);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		_registryReadable = NO;
		memset(&_registry, 0, sizeof(_registry));
		_registryProblem = [NSString stringWithFormat:@"No pude leer los registros guardados en %@ (%s), así que no "
			@"los toco.", _registryPath, chiaki_error_string(err)];
		return;
	}
	_registry = registry;
	_registryReadable = YES;
	_registryProblem = nil;
}

/** Keeps the IP each registered console answered from, on top of what is on disk now. */
- (void)rememberAddresses
{
	if(!_registryReadable || !ck_console_rows_update_registry(_discovered, _discoveredCount, &_registry))
		return;
	// ckctl may have changed the file since it was read: apply the change to what is there now.
	CKRegistry fresh;
	if(ck_registry_load(&fresh, _registryPath.fileSystemRepresentation) != CHIAKI_ERR_SUCCESS)
		return;
	if(ck_console_rows_update_registry(_discovered, _discoveredCount, &fresh)
		&& ck_registry_save(&fresh, _registryPath.fileSystemRepresentation) != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGW(&_log, "Chiakiport: no pude guardar la IP nueva en %s", _registryPath.fileSystemRepresentation);
	_registry = fresh;
}

// ---------------------------------------------------------------------------
// Searching
// ---------------------------------------------------------------------------

- (void)start
{
	if(_browser)
		return;
	// ckctl may have registered or forgotten a console meanwhile.
	[self loadRegistry];
	_generation++;
	_browserGeneration = _generation;
	_discoveredCount = 0;
	_searched = NO;
	ChiakiErrorCode err = ck_console_browser_start(&_browser, &_log, NULL, browser_cb, (__bridge void *)self);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		_browser = NULL;
		_searched = YES;
		_searchProblem = [NSString stringWithFormat:@"No pude buscar consolas en la red (%s). Revisa que el Wi-Fi "
			@"esté encendido.", chiaki_error_string(err)];
		[self rebuild];
		return;
	}
	_searchProblem = nil;
	NSUInteger generation = _generation;
	__weak CKLanConsoleService *weak = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CKFirstSearchSeconds * NSEC_PER_SEC)),
		dispatch_get_main_queue(), ^{
			[weak firstSearchEnded:generation];
		});
	[self rebuild];
}

- (void)stop
{
	if(!_browser)
		return;
	ck_console_browser_stop(_browser);
	_browser = NULL;
	_generation++;
}

- (void)firstSearchEnded:(NSUInteger)generation
{
	if(generation != _generation || _searched)
		return;
	_searched = YES;
	[self rebuild];
}

- (void)browserFound:(NSData *)consoles generation:(NSUInteger)generation
{
	if(generation != _generation)
		return;
	_discoveredCount = MIN(consoles.length / sizeof(CKConsole), (NSUInteger)CK_CONSOLES_MAX);
	memcpy(_discovered, consoles.bytes, _discoveredCount * sizeof(CKConsole));
	_searched = YES;
	[self rememberAddresses];
	[self rebuild];
}

- (void)rebuild
{
	NSMutableArray<CKConsoleItem *> *items = [NSMutableArray array];
	if(_searched)
	{
		CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
		size_t count = ck_console_rows_build(_discovered, _discoveredCount, &_registry, rows, CK_CONSOLE_ROWS_MAX);
		for(size_t i = 0; i < count; i++)
			[items addObject:[[CKConsoleItem alloc] initWithRow:&rows[i]]];
	}
	_items = [items copy];
	[_delegate consoleServiceDidChange:self];
}

// ---------------------------------------------------------------------------
// Wake-up and registration
// ---------------------------------------------------------------------------

- (void)wakeConsole:(CKConsoleItem *)console completion:(CKWakeCompletion)completion
{
	ChiakiRegisteredHost host;
	if(![self getRegisteredHost:&host forConsole:console])
	{
		completion(@"La consola no está registrada en este iPad.");
		return;
	}
	NSString *address = console.address;
	if(!address.length)
	{
		completion(@"No sé la IP de la consola. Enciéndela con el botón y búscala otra vez.");
		return;
	}
	// The block keeps self, and with it the log, until the signal is sent.
	dispatch_async(_queue, ^{
		ChiakiErrorCode err = ck_console_wakeup(&self->_log, address.UTF8String, &host);
		dispatch_async(dispatch_get_main_queue(), ^{
			if(err == CHIAKI_ERR_SUCCESS)
				completion(nil);
			else
				completion([NSString stringWithFormat:@"No pude mandar la señal a %@ (%s).", address,
					chiaki_error_string(err)]);
		});
	});
}

- (void)registerConsole:(CKConsoleItem *)console
              accountID:(const uint8_t *)accountID
                    pin:(uint32_t)pin
             completion:(CKRegistCompletion)completion
{
	completion(CK_REGIST_FAILED, @"El registro desde la app llega con A6.3. Mientras, usa ckctl regist por SSH.");
}

- (void)cancelRegistration
{
}

- (BOOL)getRegisteredHost:(ChiakiRegisteredHost *)host forConsole:(CKConsoleItem *)console
{
	CKRegistryEntry *entry = ck_registry_find_host_id(&_registry, console.hostID.UTF8String);
	if(!entry)
		return NO;
	*host = entry->host;
	return YES;
}

@end
