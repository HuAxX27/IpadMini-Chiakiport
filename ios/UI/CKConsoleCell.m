// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKConsoleCell.h"

#import "CKTheme.h"

static const CGFloat CKCellHeight = 84;
static const CGFloat CKCellMargin = 24;
static const CGFloat CKButtonWidth = 150;
static const CGFloat CKButtonHeight = 44;

@implementation CKConsoleCell
{
	UILabel *_badgeLabel;
	UILabel *_nameLabel;
	UILabel *_detailLabel;
	UILabel *_busyLabel;
	UIActivityIndicatorView *_spinner;
}

+ (CGFloat)height
{
	return CKCellHeight;
}

- (instancetype)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString *)reuseIdentifier
{
	self = [super initWithStyle:UITableViewCellStyleDefault reuseIdentifier:reuseIdentifier];
	if(!self)
		return nil;
	self.backgroundColor = CKThemeCell();
	UIView *selected = [[UIView alloc] init];
	selected.backgroundColor = CKThemeSeparator();
	self.selectedBackgroundView = selected;

	_badgeLabel = [[UILabel alloc] init];
	_badgeLabel.font = [UIFont boldSystemFontOfSize:15];
	_badgeLabel.textAlignment = NSTextAlignmentCenter;
	_badgeLabel.textColor = CKThemeText();
	_badgeLabel.layer.cornerRadius = 8;
	_badgeLabel.layer.masksToBounds = YES;
	[self.contentView addSubview:_badgeLabel];

	_nameLabel = [[UILabel alloc] init];
	_nameLabel.font = [UIFont systemFontOfSize:22 weight:UIFontWeightMedium];
	_nameLabel.textColor = CKThemeText();
	[self.contentView addSubview:_nameLabel];

	_detailLabel = [[UILabel alloc] init];
	_detailLabel.font = [UIFont systemFontOfSize:16];
	_detailLabel.textColor = CKThemeSecondaryText();
	[self.contentView addSubview:_detailLabel];

	_actionButton = [UIButton buttonWithType:UIButtonTypeSystem];
	_actionButton.titleLabel.font = [UIFont systemFontOfSize:18 weight:UIFontWeightSemibold];
	_actionButton.layer.cornerRadius = 10;
	_actionButton.layer.borderWidth = 1.5;
	[self.contentView addSubview:_actionButton];

	_spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
	_spinner.hidesWhenStopped = YES;
	[self.contentView addSubview:_spinner];

	_busyLabel = [[UILabel alloc] init];
	_busyLabel.font = [UIFont systemFontOfSize:16];
	_busyLabel.textColor = CKThemeSecondaryText();
	[self.contentView addSubview:_busyLabel];
	return self;
}

- (void)showConsole:(CKConsoleItem *)console busyText:(NSString *)busyText
{
	CKConsoleRow row = console.row;
	_badgeLabel.text = row.ps5 ? @"PS5" : @"PS4";
	_nameLabel.text = console.name.length ? console.name : @"(sin nombre)";

	UIColor *stateColor;
	switch(row.state)
	{
		case CK_CONSOLE_ROW_READY:
			stateColor = CKThemeReady();
			break;
		case CK_CONSOLE_ROW_STANDBY:
			stateColor = CKThemeStandby();
			break;
		default:
			stateColor = CKThemeSecondaryText();
			break;
	}
	_badgeLabel.backgroundColor = [stateColor colorWithAlphaComponent:0.25];

	NSMutableArray<NSString *> *parts = [NSMutableArray array];
	[parts addObject:@(ck_console_row_state_string(row.state))];
	if(console.address.length)
		[parts addObject:console.address];
	if(!row.registered)
		[parts addObject:row.state == CK_CONSOLE_ROW_READY ? @"sin registrar" : @"sin registrar: enciéndela para registrarla"];
	if(console.runningGame.length)
		[parts addObject:console.runningGame];
	NSString *detail = [parts componentsJoinedByString:@" · "];
	NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:detail];
	NSUInteger stateLength = [parts[0] length];
	[text addAttribute:NSForegroundColorAttributeName value:stateColor range:NSMakeRange(0, stateLength)];
	_detailLabel.attributedText = text;

	NSString *action = @(ck_console_action_string(row.action));
	BOOL busy = busyText != nil;
	_actionButton.hidden = busy || action.length == 0;
	[_actionButton setTitle:action forState:UIControlStateNormal];
	UIColor *tint = row.action == CK_CONSOLE_ACTION_PLAY ? CKThemeReady() : CKThemeAccent();
	[_actionButton setTitleColor:tint forState:UIControlStateNormal];
	_actionButton.layer.borderColor = tint.CGColor;
	_busyLabel.text = busyText;
	_busyLabel.hidden = !busy;
	if(busy)
		[_spinner startAnimating];
	else
		[_spinner stopAnimating];
	[self setNeedsLayout];
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	CGRect bounds = self.contentView.bounds;
	CGFloat midY = CGRectGetMidY(bounds);
	_badgeLabel.frame = CGRectMake(CKCellMargin, midY - 18, 56, 36);

	CGFloat right = bounds.size.width - CKCellMargin;
	_actionButton.frame = CGRectMake(right - CKButtonWidth, midY - CKButtonHeight / 2, CKButtonWidth, CKButtonHeight);
	CGSize busySize = [_busyLabel sizeThatFits:CGSizeMake(CKButtonWidth * 2, CKButtonHeight)];
	_busyLabel.frame = CGRectMake(right - busySize.width, midY - busySize.height / 2, busySize.width, busySize.height);
	_spinner.center = CGPointMake(CGRectGetMinX(_busyLabel.frame) - 20, midY);

	CGFloat textX = CGRectGetMaxX(_badgeLabel.frame) + 18;
	CGFloat textRight = (_busyLabel.hidden ? CGRectGetMinX(_actionButton.frame) : CGRectGetMinX(_spinner.frame)) - 16;
	CGFloat width = MAX(0, textRight - textX);
	_nameLabel.frame = CGRectMake(textX, midY - 30, width, 30);
	_detailLabel.frame = CGRectMake(textX, midY + 4, width, 24);
}

@end
