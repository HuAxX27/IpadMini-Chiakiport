// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKTouchControlsView.h"

#include "ck_touch_controls.h"

// See-through enough to play over the picture; a pressed control lights up.
static const CGFloat kIdleAlpha = 0.4;
static const CGFloat kPressedAlpha = 0.9;

/**
 * One control, drawn once into its own small view. Pressing only changes its
 * alpha, so the A5 never redraws during play.
 */
@interface CKTouchControlShape : UIView
- (instancetype)initWithControl:(const CKTouchControl *)control;
@end

@implementation CKTouchControlShape
{
	CKTouchControl _control;
}

- (instancetype)initWithControl:(const CKTouchControl *)control
{
	self = [super initWithFrame:CGRectMake(control->x, control->y, control->w, control->h)];
	if(!self)
		return nil;
	_control = *control;
	self.opaque = NO;
	self.backgroundColor = [UIColor clearColor];
	// The controls view takes every touch itself: hit testing must not stop here.
	self.userInteractionEnabled = NO;
	self.alpha = kIdleAlpha;
	return self;
}

static UIBezierPath *arrow(CGPoint tip, CGFloat size, CGFloat dx, CGFloat dy)
{
	// A triangle pointing along (dx, dy), its tip at tip.
	UIBezierPath *path = [UIBezierPath bezierPath];
	[path moveToPoint:tip];
	[path addLineToPoint:CGPointMake(tip.x - dx * size - dy * size * 0.8, tip.y - dy * size + dx * size * 0.8)];
	[path addLineToPoint:CGPointMake(tip.x - dx * size + dy * size * 0.8, tip.y - dy * size - dx * size * 0.8)];
	[path closePath];
	return path;
}

- (void)drawRect:(CGRect)rect
{
	(void)rect;
	CGRect r = CGRectInset(self.bounds, 1.5, 1.5);
	CGFloat side = MIN(r.size.width, r.size.height);
	UIColor *line = [UIColor colorWithWhite:1.0 alpha:0.9];
	UIColor *fill = [UIColor colorWithWhite:1.0 alpha:0.25];
	UIBezierPath *shape;
	switch(_control.kind)
	{
		case CK_TOUCH_DPAD:
		case CK_TOUCH_STICK_LEFT:
		case CK_TOUCH_STICK_RIGHT:
			shape = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(r, (r.size.width - side) / 2,
				(r.size.height - side) / 2)];
			break;
		case CK_TOUCH_TOUCHPAD:
			shape = [UIBezierPath bezierPathWithRoundedRect:r cornerRadius:14];
			break;
		case CK_TOUCH_BUTTON:
			shape = _control.round ? [UIBezierPath bezierPathWithOvalInRect:r]
				: [UIBezierPath bezierPathWithRoundedRect:r cornerRadius:10];
			break;
	}
	[fill setFill];
	[shape fill];
	[line setStroke];
	shape.lineWidth = 2;
	[shape stroke];

	CGPoint c = CGPointMake(CGRectGetMidX(r), CGRectGetMidY(r));
	if(_control.kind == CK_TOUCH_DPAD)
	{
		[line setFill];
		CGFloat reach = side * 0.42, size = side * 0.14;
		[arrow(CGPointMake(c.x, c.y - reach), size, 0, -1) fill];
		[arrow(CGPointMake(c.x, c.y + reach), size, 0, 1) fill];
		[arrow(CGPointMake(c.x - reach, c.y), size, -1, 0) fill];
		[arrow(CGPointMake(c.x + reach, c.y), size, 1, 0) fill];
		return;
	}

	NSString *label = [NSString stringWithUTF8String:_control.label];
	if(label.length == 0)
		return;
	CGFloat points;
	if(_control.kind == CK_TOUCH_TOUCHPAD)
		points = 17;
	else if(_control.kind == CK_TOUCH_BUTTON && label.length == 1)
		points = MIN(r.size.height * 0.5, 36); // the face buttons' symbols
	else
		points = MIN(r.size.height * 0.4, 22);
	NSDictionary *attributes = @{
		NSFontAttributeName: [UIFont boldSystemFontOfSize:points],
		NSForegroundColorAttributeName: line,
	};
	CGSize size = [label sizeWithAttributes:attributes];
	[label drawAtPoint:CGPointMake(c.x - size.width / 2, c.y - size.height / 2) withAttributes:attributes];
}

@end

@implementation CKTouchControlsView
{
	CKTouchControls _controls;
	BOOL _ready;
	CGSize _laidOutSize;
	NSMutableArray<CKTouchControlShape *> *_shapes;
	/** The stick's knob, by control index; NSNull for the other controls. */
	NSMutableArray *_knobs;
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if(self)
		[self setUp];
	return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super initWithCoder:coder];
	if(self)
		[self setUp];
	return self;
}

- (void)setUp
{
	self.opaque = NO;
	self.backgroundColor = [UIColor clearColor];
	self.multipleTouchEnabled = YES;
	_shapes = [NSMutableArray array];
	_knobs = [NSMutableArray array];
	CKTouchLayout layout;
	ck_touch_layout_default(&layout, (float)self.bounds.size.width, (float)self.bounds.size.height);
	_ready = ck_touch_controls_init(&_controls, &layout);
	_laidOutSize = self.bounds.size;
	[self buildShapes];
}

- (void)dealloc
{
	if(_ready)
		ck_touch_controls_fini(&_controls);
}

- (CKInputSource)inputSource
{
	if(!_ready)
	{
		CKInputSource none = { CK_TOUCH_CONTROLS_NAME, NULL, NULL };
		return none;
	}
	return ck_touch_controls_input(&_controls);
}

- (void)buildShapes
{
	for(UIView *view in _shapes)
		[view removeFromSuperview];
	for(id knob in _knobs)
	{
		if(knob != [NSNull null])
			[(UIView *)knob removeFromSuperview];
	}
	[_shapes removeAllObjects];
	[_knobs removeAllObjects];
	if(!_ready)
		return;
	const CKTouchLayout *layout = &_controls.layout;
	for(unsigned i = 0; i < layout->count; i++)
	{
		const CKTouchControl *control = &layout->controls[i];
		CKTouchControlShape *shape = [[CKTouchControlShape alloc] initWithControl:control];
		[self addSubview:shape];
		[_shapes addObject:shape];
		if(control->kind != CK_TOUCH_STICK_LEFT && control->kind != CK_TOUCH_STICK_RIGHT)
		{
			[_knobs addObject:[NSNull null]];
			continue;
		}
		CGFloat side = MIN(control->w, control->h) * 0.36;
		UIView *knob = [[UIView alloc] initWithFrame:CGRectMake(0, 0, side, side)];
		knob.userInteractionEnabled = NO;
		knob.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.6];
		knob.layer.cornerRadius = side / 2;
		knob.hidden = YES;
		[_knobs addObject:knob];
	}
	// Knobs go over every shape.
	for(id knob in _knobs)
	{
		if(knob != [NSNull null])
			[self addSubview:knob];
	}
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	if(!_ready || CGSizeEqualToSize(self.bounds.size, _laidOutSize))
		return;
	_laidOutSize = self.bounds.size;
	CKTouchLayout layout;
	ck_touch_layout_default(&layout, (float)_laidOutSize.width, (float)_laidOutSize.height);
	ck_touch_controls_set_layout(&_controls, &layout);
	[self buildShapes];
}

/** Shows what is pressed and where the sticks are. */
- (void)refresh
{
	uint32_t pressed = ck_touch_controls_pressed(&_controls);
	for(NSUInteger i = 0; i < _shapes.count; i++)
	{
		CGFloat alpha = (pressed & (1u << i)) ? kPressedAlpha : kIdleAlpha;
		if(_shapes[i].alpha != alpha)
			_shapes[i].alpha = alpha;
		id knob = _knobs[i];
		if(knob == [NSNull null])
			continue;
		float dx, dy;
		UIView *view = knob;
		if(ck_touch_controls_stick_offset(&_controls, (int)i, &dx, &dy))
		{
			CGRect frame = _shapes[i].frame;
			view.center = CGPointMake(CGRectGetMidX(frame) + dx, CGRectGetMidY(frame) + dy);
			view.hidden = NO;
		}
		else
			view.hidden = YES;
	}
}

- (void)releaseAllControls
{
	if(!_ready)
		return;
	ck_touch_controls_release_all(&_controls);
	[self refresh];
}

- (void)setHidden:(BOOL)hidden
{
	[super setHidden:hidden];
	if(hidden)
		[self releaseAllControls];
}

- (void)willMoveToWindow:(UIWindow *)newWindow
{
	[super willMoveToWindow:newWindow];
	if(!newWindow)
		[self releaseAllControls];
}

- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event
{
	(void)event;
	return _ready && ck_touch_layout_hit(&_controls.layout, (float)point.x, (float)point.y) >= 0;
}

static uintptr_t touch_key(UITouch *touch)
{
	return (uintptr_t)(__bridge void *)touch;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	(void)event;
	if(!_ready)
		return;
	for(UITouch *touch in touches)
	{
		CGPoint p = [touch locationInView:self];
		ck_touch_controls_began(&_controls, touch_key(touch), (float)p.x, (float)p.y);
	}
	[self refresh];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	(void)event;
	if(!_ready)
		return;
	for(UITouch *touch in touches)
	{
		CGPoint p = [touch locationInView:self];
		ck_touch_controls_moved(&_controls, touch_key(touch), (float)p.x, (float)p.y);
	}
	[self refresh];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	(void)event;
	if(!_ready)
		return;
	for(UITouch *touch in touches)
		ck_touch_controls_ended(&_controls, touch_key(touch));
	[self refresh];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	[self touchesEnded:touches withEvent:event];
}

@end
