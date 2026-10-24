// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKSettingsViewController.h"

#import "CKTheme.h"

typedef NS_ENUM(NSInteger, CKSettingsSection) {
	CKSettingsSectionVideo,
	CKSettingsSectionReset,
	CKSettingsSectionTesting,
	CKSettingsSectionAbout,
	CKSettingsSectionCount,
};

typedef NS_ENUM(NSInteger, CKVideoRow) {
	CKVideoRowResolution,
	CKVideoRowFps,
	CKVideoRowBitrate,
	CKVideoRowCount,
};

static const CGFloat CKSegmentWidth = 420;

@implementation CKSettingsViewController
{
	CKSettingsStore *_store;
	UISegmentedControl *_resolution;
	UISegmentedControl *_fps;
	UISegmentedControl *_bitrate;
	UISwitch *_simulated;
}

- (instancetype)initWithStore:(CKSettingsStore *)store
{
	self = [super initWithStyle:UITableViewStyleGrouped];
	if(!self)
		return nil;
	_store = store;
	self.title = @"Ajustes";
	return self;
}

- (UISegmentedControl *)segmentsWithCount:(int)count label:(const char *(^)(int index))label
{
	NSMutableArray<NSString *> *items = [NSMutableArray array];
	for(int i = 0; i < count; i++)
		[items addObject:@(label(i))];
	UISegmentedControl *control = [[UISegmentedControl alloc] initWithItems:items];
	control.frame = CGRectMake(0, 0, CKSegmentWidth, 32);
	[control addTarget:self action:@selector(segmentChanged:) forControlEvents:UIControlEventValueChanged];
	return control;
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.tableView.backgroundColor = CKThemeBackground();
	self.tableView.separatorColor = CKThemeSeparator();
	_resolution = [self segmentsWithCount:CK_UI_RESOLUTION_COUNT label:^const char *(int i) {
		return ck_ui_resolution_label(ck_ui_resolutions[i]);
	}];
	_fps = [self segmentsWithCount:CK_UI_FPS_COUNT label:^const char *(int i) {
		return ck_ui_fps_label(ck_ui_fps[i]);
	}];
	_bitrate = [self segmentsWithCount:CK_UI_BITRATE_COUNT label:^const char *(int i) {
		return ck_ui_bitrate_label(ck_ui_bitrates_kbps[i]);
	}];
	_simulated = [[UISwitch alloc] init];
	_simulated.onTintColor = CKThemeAccent();
	[_simulated addTarget:self action:@selector(simulatedChanged:) forControlEvents:UIControlEventValueChanged];
	[self showSettings];
}

- (void)showSettings
{
	CKSettings settings = _store.settings;
	int index = ck_ui_resolution_index(settings.resolution);
	_resolution.selectedSegmentIndex = index < 0 ? UISegmentedControlNoSegment : index;
	index = ck_ui_fps_index(settings.fps);
	_fps.selectedSegmentIndex = index < 0 ? UISegmentedControlNoSegment : index;
	// A bitrate set by hand in the file selects no segment; the footer says it.
	index = ck_ui_bitrate_index(settings.bitrate_kbps);
	_bitrate.selectedSegmentIndex = index < 0 ? UISegmentedControlNoSegment : index;
	_simulated.on = _store.simulatedConsoles;
	[self.tableView reloadData];
}

- (void)simulatedChanged:(UISwitch *)control
{
	NSError *error;
	if(![_store updateSimulatedConsoles:control.on error:&error])
		[self showSaveError:error];
	[self showSettings];
}

- (void)showSaveError:(NSError *)error
{
	NSString *message = [NSString stringWithFormat:@"Se usan hasta que cierres la app, pero no quedaron guardados "
		@"en %@: %@", _store.path, error.localizedDescription];
	UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"No pude guardar los ajustes"
		message:message preferredStyle:UIAlertControllerStyleAlert];
	[alert addAction:[UIAlertAction actionWithTitle:@"Entendido" style:UIAlertActionStyleDefault handler:nil]];
	[self presentViewController:alert animated:YES completion:nil];
}

- (void)segmentChanged:(UISegmentedControl *)control
{
	CKSettings settings = _store.settings;
	NSInteger index = control.selectedSegmentIndex;
	if(index < 0)
		return;
	if(control == _resolution && index < CK_UI_RESOLUTION_COUNT)
		settings.resolution = ck_ui_resolutions[index];
	else if(control == _fps && index < CK_UI_FPS_COUNT)
		settings.fps = ck_ui_fps[index];
	else if(control == _bitrate && index < CK_UI_BITRATE_COUNT)
		settings.bitrate_kbps = ck_ui_bitrates_kbps[index];
	[self save:settings];
}

- (void)save:(CKSettings)settings
{
	NSError *error;
	if(![_store updateSettings:settings error:&error])
		[self showSaveError:error];
	[self showSettings];
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
	return CKSettingsSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
	switch(section)
	{
		case CKSettingsSectionVideo:
			return CKVideoRowCount;
		case CKSettingsSectionAbout:
			return 2;
		default:
			return 1;
	}
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
	switch(section)
	{
		case CKSettingsSectionVideo:
			return @"Video";
		case CKSettingsSectionTesting:
			return @"Pruebas";
		case CKSettingsSectionAbout:
			return @"Acerca de";
		default:
			return nil;
	}
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
	if(section == CKSettingsSectionTesting)
		return @"Muestra cuatro consolas inventadas en vez de buscar en la red, para probar la app sin la PS5. "
			@"Nada de lo que hagas con ellas se guarda.";
	if(section != CKSettingsSectionVideo)
		return nil;
	CKSettings settings = _store.settings;
	char summary[96];
	ck_ui_settings_summary(&settings, summary, sizeof(summary));
	NSMutableString *footer = [NSMutableString stringWithFormat:@"Se usan desde la próxima sesión: %s.", summary];
	if(ck_ui_bitrate_index(settings.bitrate_kbps) < 0)
		[footer appendString:@" El bitrate está puesto a mano en el archivo de ajustes; elige uno para cambiarlo."];
	[footer appendString:@"\n\nLo probado en el iPad mini es 540p a 30 cuadros por segundo. 720p y 60 cuadros por "
		@"segundo están por probarse: si el video se traba, vuelve a 540p y 30. Siempre es H.264: el A5 no decodifica "
		@"H.265."];
	return footer;
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

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
	return 56;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
	UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
	cell.backgroundColor = CKThemeCell();
	cell.textLabel.textColor = CKThemeText();
	cell.textLabel.font = [UIFont systemFontOfSize:19];
	cell.detailTextLabel.textColor = CKThemeSecondaryText();
	cell.selectionStyle = UITableViewCellSelectionStyleNone;

	switch(indexPath.section)
	{
		case CKSettingsSectionVideo:
		{
			NSArray<NSString *> *titles = @[ @"Resolución", @"Cuadros por segundo", @"Bitrate (Mbps)" ];
			NSArray<UISegmentedControl *> *controls = @[ _resolution, _fps, _bitrate ];
			cell.textLabel.text = titles[(NSUInteger)indexPath.row];
			cell.accessoryView = controls[(NSUInteger)indexPath.row];
			break;
		}
		case CKSettingsSectionTesting:
			cell.textLabel.text = @"Consolas simuladas";
			cell.accessoryView = _simulated;
			break;
		case CKSettingsSectionReset:
			cell.textLabel.text = @"Volver a los valores por defecto";
			cell.textLabel.textColor = CKThemeAccent();
			cell.selectionStyle = UITableViewCellSelectionStyleDefault;
			break;
		default:
			if(indexPath.row == 0)
			{
				NSDictionary *info = [NSBundle mainBundle].infoDictionary;
				cell.textLabel.text = @"Versión";
				cell.detailTextLabel.text = [NSString stringWithFormat:@"%@ (%s)",
					info[@"CFBundleShortVersionString"], CK_GIT_COMMIT];
			}
			else
			{
				cell.textLabel.text = @"Archivo de ajustes";
				cell.detailTextLabel.text = _store.path;
			}
			break;
	}
	return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
	[tableView deselectRowAtIndexPath:indexPath animated:YES];
	if(indexPath.section != CKSettingsSectionReset)
		return;
	CKSettings settings;
	ck_settings_set_default(&settings);
	[self save:settings];
}

@end
