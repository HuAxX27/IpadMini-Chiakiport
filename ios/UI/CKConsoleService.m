// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKConsoleService.h"

static NSString *CKString(const char *text)
{
	// The console's own text may be cut in the middle of a character; never crash on it.
	NSString *string = [NSString stringWithUTF8String:text];
	return string ? string : [NSString stringWithCString:text encoding:NSISOLatin1StringEncoding];
}

@implementation CKConsoleItem

- (instancetype)initWithRow:(const CKConsoleRow *)row
{
	self = [super init];
	if(!self)
		return nil;
	_row = *row;
	_name = CKString(row->name);
	_address = CKString(row->addr);
	_hostID = CKString(row->host_id);
	_runningGame = CKString(row->running_app_name);
	return self;
}

- (BOOL)isEqual:(id)object
{
	if(![object isKindOfClass:[CKConsoleItem class]])
		return NO;
	CKConsoleItem *other = object;
	return [_hostID isEqualToString:other->_hostID] && [_name isEqualToString:other->_name]
		&& [_address isEqualToString:other->_address] && [_runningGame isEqualToString:other->_runningGame]
		&& _row.state == other->_row.state && _row.registered == other->_row.registered
		&& _row.action == other->_row.action;
}

- (NSUInteger)hash
{
	return _hostID.hash;
}

@end
