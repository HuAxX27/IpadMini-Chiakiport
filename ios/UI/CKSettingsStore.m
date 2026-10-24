// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKSettingsStore.h"

NSString *const CKSettingsDidChangeNotification = @"CKSettingsDidChangeNotification";

static NSString *const CKSettingsErrorDomain = @"CKSettingsErrorDomain";
static NSString *const CKKeyResolution = @"resolution";
static NSString *const CKKeyFps = @"fps";
static NSString *const CKKeyBitrate = @"bitrate_kbps";
static NSString *const CKKeySimulated = @"simulated_consoles";

/** The number under key, or -1 when it is missing or not a number. */
static int64_t CKNumber(NSDictionary *dict, NSString *key)
{
	id value = dict[key];
	return [value isKindOfClass:[NSNumber class]] ? [(NSNumber *)value longLongValue] : -1;
}

@implementation CKSettingsStore

+ (instancetype)sharedStore
{
	static CKSettingsStore *store;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		store = [[CKSettingsStore alloc] initWithPath:@(CK_UI_SETTINGS_PATH)];
	});
	return store;
}

- (instancetype)initWithPath:(NSString *)path
{
	self = [super init];
	if(!self)
		return nil;
	_path = [path copy];
	ck_settings_set_default(&_settings);
	NSData *data = [NSData dataWithContentsOfFile:path];
	if(!data)
		return self;
	id plist = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL
		error:NULL];
	if(![plist isKindOfClass:[NSDictionary class]])
	{
		NSLog(@"Chiakiport: %@ no es un diccionario; uso los ajustes por defecto", path);
		return self;
	}
	NSDictionary *dict = plist;
	id simulated = dict[CKKeySimulated];
	_simulatedConsoles = [simulated isKindOfClass:[NSNumber class]] && [(NSNumber *)simulated boolValue];
	if(!ck_ui_settings_from_values(CKNumber(dict, CKKeyResolution), CKNumber(dict, CKKeyFps),
		CKNumber(dict, CKKeyBitrate), &_settings))
		NSLog(@"Chiakiport: %@ tiene valores que no ofrezco; esos van por defecto", path);
	return self;
}

- (BOOL)updateSettings:(CKSettings)settings error:(NSError **)error
{
	if(!ck_ui_settings_valid(&settings))
	{
		if(error)
			*error = [NSError errorWithDomain:CKSettingsErrorDomain code:1
				userInfo:@{ NSLocalizedDescriptionKey: @"La app no ofrece esos ajustes." }];
		return NO;
	}
	_settings = settings;
	[[NSNotificationCenter defaultCenter] postNotificationName:CKSettingsDidChangeNotification object:self];
	return [self save:error];
}

- (BOOL)updateSimulatedConsoles:(BOOL)simulated error:(NSError **)error
{
	_simulatedConsoles = simulated;
	[[NSNotificationCenter defaultCenter] postNotificationName:CKSettingsDidChangeNotification object:self];
	return [self save:error];
}

- (BOOL)save:(NSError **)error
{
	int64_t height, fps, bitrate;
	ck_ui_settings_to_values(&_settings, &height, &fps, &bitrate);
	NSDictionary *dict = @{ CKKeyResolution: @(height), CKKeyFps: @(fps), CKKeyBitrate: @(bitrate),
		CKKeySimulated: @(_simulatedConsoles) };
	NSData *data = [NSPropertyListSerialization dataWithPropertyList:dict format:NSPropertyListXMLFormat_v1_0
		options:0 error:error];
	if(!data)
		return NO;
	NSString *folder = [_path stringByDeletingLastPathComponent];
	if(![[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil
		error:error])
		return NO;
	// Through a temporary file renamed over the old one: a crash never leaves half a file.
	return [data writeToFile:_path options:NSDataWritingAtomic error:error];
}

@end
