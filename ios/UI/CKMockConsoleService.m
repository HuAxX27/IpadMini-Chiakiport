// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKMockConsoleService.h"

#include <string.h>

/** Seconds a simulated console takes to turn on: a real PS5 takes 10 to 20. */
static const double CKMockWakeSeconds = 5.0;
/** A real registration takes up to 6 s. */
static const double CKMockRegistSeconds = 2.0;
static const uint32_t CKMockWrongPin = 0;

static void mock_console(CKConsole *console, const char *name, const char *addr, const char *host_id,
	ChiakiDiscoveryHostState state, const char *game)
{
	memset(console, 0, sizeof(*console));
	strncpy(console->name, name, sizeof(console->name) - 1);
	strncpy(console->addr, addr, sizeof(console->addr) - 1);
	strncpy(console->host_id, host_id, sizeof(console->host_id) - 1);
	strncpy(console->system_version, "09060000", sizeof(console->system_version) - 1);
	strncpy(console->running_app_name, game, sizeof(console->running_app_name) - 1);
	console->state = state;
	console->ps5 = true;
	console->target = CHIAKI_TARGET_PS5_1;
}

static BOOL mac_from_hex(const char *hex, uint8_t mac[6])
{
	if(strlen(hex) != 12)
		return NO;
	for(size_t i = 0; i < 12; i++)
	{
		char c = hex[i];
		int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		if(v < 0)
			return NO;
		if(i % 2)
			mac[i / 2] = (uint8_t)(mac[i / 2] | v);
		else
			mac[i / 2] = (uint8_t)(v << 4);
	}
	return YES;
}

/** A registration with made-up keys: the simulation never talks to a console. */
static void mock_host(ChiakiRegisteredHost *host, const char *name, const char *host_id)
{
	memset(host, 0, sizeof(*host));
	host->target = CHIAKI_TARGET_PS5_1;
	strncpy(host->server_nickname, name, sizeof(host->server_nickname) - 1);
	mac_from_hex(host_id, host->server_mac);
	memcpy(host->rp_regist_key, "f0f0f0f0", 8);
	memset(host->rp_key, 0xf0, sizeof(host->rp_key));
}

@implementation CKMockConsoleService
{
	CKConsole _discovered[CK_CONSOLES_MAX];
	size_t _discoveredCount;
	CKRegistry _registry;
	NSArray<CKConsoleItem *> *_items;
	CKRegistCompletion _registCompletion;
}

@synthesize delegate = _delegate;

- (instancetype)init
{
	self = [super init];
	if(!self)
		return nil;
	mock_console(&_discovered[0], "PS5 de la sala", "192.168.0.20", "A0B1C2D3E4F5", CHIAKI_DISCOVERY_HOST_STATE_READY,
		"Astro Bot");
	mock_console(&_discovered[1], "PS5 del cuarto", "192.168.0.21", "A0B1C2D3E4F6", CHIAKI_DISCOVERY_HOST_STATE_STANDBY, "");
	mock_console(&_discovered[2], "PS5-517", "192.168.0.22", "A0B1C2D3E4F7", CHIAKI_DISCOVERY_HOST_STATE_READY, "");
	_discoveredCount = 3;

	memset(&_registry, 0, sizeof(_registry));
	ChiakiRegisteredHost host;
	mock_host(&host, "PS5 de la sala", "A0B1C2D3E4F5");
	ck_registry_put(&_registry, &host, "192.168.0.20");
	mock_host(&host, "PS5 del cuarto", "A0B1C2D3E4F6");
	ck_registry_put(&_registry, &host, "192.168.0.21");
	mock_host(&host, "PS5 de la oficina", "A0B1C2D3E4F8");
	ck_registry_put(&_registry, &host, "10.0.0.5");

	[self rebuild];
	return self;
}

- (BOOL)isSimulated
{
	return YES;
}

- (NSArray<CKConsoleItem *> *)consoles
{
	return _items;
}

- (NSString *)problem
{
	return nil;
}

- (void)start
{
}

- (void)stop
{
}

- (void)rebuild
{
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
	size_t count = ck_console_rows_build(_discovered, _discoveredCount, &_registry, rows, CK_CONSOLE_ROWS_MAX);
	NSMutableArray<CKConsoleItem *> *items = [NSMutableArray arrayWithCapacity:count];
	for(size_t i = 0; i < count; i++)
		[items addObject:[[CKConsoleItem alloc] initWithRow:&rows[i]]];
	_items = [items copy];
	[_delegate consoleServiceDidChange:self];
}

- (CKConsole *)discoveredConsoleWithHostID:(NSString *)hostID
{
	for(size_t i = 0; i < _discoveredCount; i++)
		if(!strcmp(_discovered[i].host_id, hostID.UTF8String))
			return &_discovered[i];
	return NULL;
}

- (void)wakeConsole:(CKConsoleItem *)console completion:(CKWakeCompletion)completion
{
	NSString *hostID = console.hostID;
	__weak CKMockConsoleService *weak = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		completion(nil);
	});
	// The one that does not answer stays off, as a console unplugged would.
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CKMockWakeSeconds * NSEC_PER_SEC)),
		dispatch_get_main_queue(), ^{
			[weak finishWakingConsoleWithHostID:hostID];
		});
}

- (void)finishWakingConsoleWithHostID:(NSString *)hostID
{
	CKConsole *console = [self discoveredConsoleWithHostID:hostID];
	if(!console || console->state == CHIAKI_DISCOVERY_HOST_STATE_READY)
		return;
	console->state = CHIAKI_DISCOVERY_HOST_STATE_READY;
	[self rebuild];
}

- (void)registerConsole:(CKConsoleItem *)console
              accountID:(const uint8_t *)accountID
                    pin:(uint32_t)pin
             completion:(CKRegistCompletion)completion
{
	if(_registCompletion)
	{
		completion(CK_REGIST_FAILED, @"Ya hay un registro en curso.");
		return;
	}
	CKRegistCompletion mine = [completion copy];
	_registCompletion = mine;
	CKConsoleRow row = console.row;
	__weak CKMockConsoleService *weak = self;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CKMockRegistSeconds * NSEC_PER_SEC)),
		dispatch_get_main_queue(), ^{
			[weak finishRegistration:mine row:row pin:pin];
		});
}

- (void)finishRegistration:(CKRegistCompletion)completion row:(CKConsoleRow)row pin:(uint32_t)pin
{
	// Canceled meanwhile, and maybe a new registration started.
	if(_registCompletion != completion)
		return;
	_registCompletion = nil;
	if(pin == CKMockWrongPin)
	{
		completion(CK_REGIST_WRONG_PIN, @(ck_regist_result_string(CK_REGIST_WRONG_PIN)));
		return;
	}
	ChiakiRegisteredHost host;
	mock_host(&host, row.name, row.host_id);
	if(ck_registry_put(&_registry, &host, row.addr) != CHIAKI_ERR_SUCCESS)
	{
		completion(CK_REGIST_FAILED, @"Ya hay 8 consolas registradas.");
		return;
	}
	[self rebuild];
	completion(CK_REGIST_SUCCESS, @(ck_regist_result_string(CK_REGIST_SUCCESS)));
}

- (void)cancelRegistration
{
	CKRegistCompletion completion = _registCompletion;
	_registCompletion = nil;
	if(completion)
		completion(CK_REGIST_CANCELED, @(ck_regist_result_string(CK_REGIST_CANCELED)));
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
