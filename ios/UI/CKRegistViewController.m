// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKRegistViewController.h"

#import "CKTheme.h"

typedef NS_ENUM(NSInteger, CKRegistSection) {
	CKRegistSectionConsole,
	CKRegistSectionAccount,
	CKRegistSectionPin,
	CKRegistSectionButton,
	CKRegistSectionCount,
};

/** The list shows the registered console; this long to read the result first. */
static const NSTimeInterval CKRegistDoneDelay = 1.5;

@implementation CKRegistViewController
{
	id<CKConsoleService> _service;
	CKConsoleItem *_console;
	UITextField *_accountField;
	UITextField *_pinField;
	BOOL _running;
	BOOL _done;
	NSString *_status;
	UIColor *_statusColor;
}

- (instancetype)initWithService:(id<CKConsoleService>)service console:(CKConsoleItem *)console
{
	self = [super initWithStyle:UITableViewStyleGrouped];
	if(!self)
		return nil;
	_service = service;
	_console = console;
	self.title = @"Registrar";
	return self;
}

- (UITextField *)fieldWithPlaceholder:(NSString *)placeholder
{
	UITextField *field = [[UITextField alloc] init];
	field.font = [UIFont systemFontOfSize:20];
	field.textColor = CKThemeText();
	field.attributedPlaceholder = [[NSAttributedString alloc] initWithString:placeholder
		attributes:@{ NSForegroundColorAttributeName: CKThemeSecondaryText() }];
	field.autocorrectionType = UITextAutocorrectionTypeNo;
	field.autocapitalizationType = UITextAutocapitalizationTypeNone;
	field.spellCheckingType = UITextSpellCheckingTypeNo;
	field.keyboardAppearance = UIKeyboardAppearanceDark;
	field.clearButtonMode = UITextFieldViewModeWhileEditing;
	field.delegate = self;
	return field;
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.tableView.backgroundColor = CKThemeBackground();
	self.tableView.separatorColor = CKThemeSeparator();

	_accountField = [self fieldWithPlaceholder:@"12 caracteres que terminan en =, o el número"];
	_accountField.keyboardType = UIKeyboardTypeASCIICapable;
	_accountField.returnKeyType = UIReturnKeyNext;
	_pinField = [self fieldWithPlaceholder:@"8 cifras"];
	_pinField.keyboardType = UIKeyboardTypeNumberPad;
	_pinField.returnKeyType = UIReturnKeyDone;
}

- (void)viewDidAppear:(BOOL)animated
{
	[super viewDidAppear:animated];
	if(!_accountField.text.length)
		[_accountField becomeFirstResponder];
}

- (void)viewWillDisappear:(BOOL)animated
{
	[super viewWillDisappear:animated];
	if(_running && self.isMovingFromParentViewController)
		[_service cancelRegistration];
}

// ---------------------------------------------------------------------------
// The form
// ---------------------------------------------------------------------------

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
	return CKRegistSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
	return section == CKRegistSectionButton && _status ? 2 : 1;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
	switch(section)
	{
		case CKRegistSectionConsole:
			return @"Consola";
		case CKRegistSectionAccount:
			return @"Account ID de PSN";
		case CKRegistSectionPin:
			return @"PIN";
		default:
			return nil;
	}
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
	switch(section)
	{
		case CKRegistSectionAccount:
			return @"El de la cuenta de PSN que usas en la PS5: en base64 (12 caracteres que terminan en =, como lo "
				@"muestra chiaki-ng) o el número en decimal. Se manda solo a la consola.";
		case CKRegistSectionPin:
			return @"En la PS5: Ajustes › Sistema › Remote Play › Vincular dispositivo. Escribe las 8 cifras mientras "
				@"se ven en la tele: cada código dura unos minutos.";
		default:
			return nil;
	}
}

- (void)tableView:(UITableView *)tableView willDisplayHeaderView:(UIView *)view forSection:(NSInteger)section
{
	if([view isKindOfClass:[UITableViewHeaderFooterView class]])
		((UITableViewHeaderFooterView *)view).textLabel.textColor = CKThemeSecondaryText();
}

- (void)tableView:(UITableView *)tableView willDisplayFooterView:(UIView *)view forSection:(NSInteger)section
{
	if([view isKindOfClass:[UITableViewHeaderFooterView class]])
		((UITableViewHeaderFooterView *)view).textLabel.textColor = CKThemeSecondaryText();
}

- (UITableViewCell *)cellWithField:(UITextField *)field
{
	UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
	cell.backgroundColor = CKThemeCell();
	cell.selectionStyle = UITableViewCellSelectionStyleNone;
	field.frame = CGRectInset(cell.contentView.bounds, 16, 4);
	field.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	field.enabled = !_running && !_done;
	[cell.contentView addSubview:field];
	return cell;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
	switch(indexPath.section)
	{
		case CKRegistSectionAccount:
			return [self cellWithField:_accountField];
		case CKRegistSectionPin:
			return [self cellWithField:_pinField];
		default:
			break;
	}

	UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
	cell.backgroundColor = CKThemeCell();
	cell.textLabel.textColor = CKThemeText();
	cell.detailTextLabel.textColor = CKThemeSecondaryText();
	cell.textLabel.font = [UIFont systemFontOfSize:20];
	if(indexPath.section == CKRegistSectionConsole)
	{
		cell.selectionStyle = UITableViewCellSelectionStyleNone;
		cell.textLabel.text = _console.name;
		cell.detailTextLabel.text = _console.address;
		return cell;
	}

	if(indexPath.row == 1)
	{
		cell.selectionStyle = UITableViewCellSelectionStyleNone;
		cell.textLabel.text = _status;
		cell.textLabel.textColor = _statusColor;
		cell.textLabel.font = [UIFont systemFontOfSize:17];
		cell.textLabel.numberOfLines = 0;
		return cell;
	}

	cell.textLabel.textAlignment = NSTextAlignmentCenter;
	if(_running)
	{
		cell.textLabel.text = @"Registrando…";
		cell.textLabel.textColor = CKThemeSecondaryText();
		UIActivityIndicatorView *spinner =
			[[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
		[spinner startAnimating];
		cell.accessoryView = spinner;
		cell.selectionStyle = UITableViewCellSelectionStyleNone;
	}
	else
	{
		cell.textLabel.text = _done ? @"Registrada" : @"Registrar";
		cell.textLabel.textColor = _done ? CKThemeReady() : CKThemeAccent();
		cell.textLabel.font = [UIFont systemFontOfSize:20 weight:UIFontWeightSemibold];
		cell.selectionStyle = _done ? UITableViewCellSelectionStyleNone : UITableViewCellSelectionStyleDefault;
	}
	return cell;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
	if(indexPath.section == CKRegistSectionButton && indexPath.row == 1)
	{
		CGFloat width = tableView.bounds.size.width - 120;
		CGRect text = [_status boundingRectWithSize:CGSizeMake(width, CGFLOAT_MAX)
			options:NSStringDrawingUsesLineFragmentOrigin attributes:@{ NSFontAttributeName: [UIFont systemFontOfSize:17] }
			context:nil];
		return MAX(52, ceil(text.size.height) + 24);
	}
	return 56;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
	[tableView deselectRowAtIndexPath:indexPath animated:YES];
	if(indexPath.section == CKRegistSectionButton && indexPath.row == 0)
		[self registerTapped];
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
	if(textField == _accountField)
		[_pinField becomeFirstResponder];
	else
		[self registerTapped];
	return NO;
}

// ---------------------------------------------------------------------------
// Registering
// ---------------------------------------------------------------------------

- (void)showStatus:(NSString *)status color:(UIColor *)color
{
	_status = status;
	_statusColor = color;
	[self.tableView reloadData];
}

- (void)registerTapped
{
	if(_running || _done)
		return;
	uint8_t accountID[CHIAKI_PSN_ACCOUNT_ID_SIZE];
	if(ck_parse_psn_account_id(_accountField.text.UTF8String ?: "", accountID) != CHIAKI_ERR_SUCCESS)
	{
		[self showStatus:@"Ese Account ID no se puede leer: deben ser 12 caracteres de base64 que terminan en =, o "
			@"el número en decimal." color:CKThemeError()];
		[_accountField becomeFirstResponder];
		return;
	}
	uint32_t pin;
	if(ck_parse_regist_pin(_pinField.text.UTF8String ?: "", &pin) != CHIAKI_ERR_SUCCESS)
	{
		[self showStatus:@"El PIN son las 8 cifras que muestra la PS5." color:CKThemeError()];
		[_pinField becomeFirstResponder];
		return;
	}

	[self.view endEditing:YES];
	_running = YES;
	[self showStatus:@"Puede tardar hasta 6 segundos." color:CKThemeSecondaryText()];
	__weak CKRegistViewController *weak = self;
	[_service registerConsole:_console accountID:accountID pin:pin completion:^(CKRegistResult result, NSString *message) {
		[weak registrationEnded:result message:message];
	}];
}

- (void)registrationEnded:(CKRegistResult)result message:(NSString *)message
{
	_running = NO;
	if(result == CK_REGIST_SUCCESS)
	{
		_done = YES;
		[self showStatus:message color:CKThemeReady()];
		__weak CKRegistViewController *weak = self;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CKRegistDoneDelay * NSEC_PER_SEC)),
			dispatch_get_main_queue(), ^{
				CKRegistViewController *strong = weak;
				if(strong.navigationController.topViewController == strong)
					[strong.navigationController popViewControllerAnimated:YES];
			});
		return;
	}
	if(result == CK_REGIST_CANCELED)
		return;
	[self showStatus:message color:CKThemeError()];
	if(result == CK_REGIST_WRONG_PIN)
	{
		_pinField.text = @"";
		[_pinField becomeFirstResponder];
	}
	else if(result == CK_REGIST_WRONG_ACCOUNT)
		[_accountField becomeFirstResponder];
}

@end
