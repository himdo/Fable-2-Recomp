// Original typographic icon; no game artwork is included.
#import <AppKit/AppKit.h>
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 2) return 1;
    NSString *folder = [NSString stringWithUTF8String:argv[1]];
    for (NSNumber *size in @[@16, @32, @128, @256, @512]) {
      for (int scale = 1; scale <= 2; ++scale) {
        int pixels = size.intValue * scale;
        NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:pixels pixelsHigh:pixels
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        NSAffineTransform *transform = [NSAffineTransform transform];
        [transform scaleBy:pixels / 512.0]; [transform concat];
        NSBezierPath *tile = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(24,24,464,464) xRadius:102 yRadius:102];
        NSGradient *gradient = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:0.08 green:0.24 blue:0.28 alpha:1]
            endingColor:[NSColor colorWithSRGBRed:0.015 green:0.055 blue:0.09 alpha:1]];
        [gradient drawInBezierPath:tile angle:-90];
        [[NSColor colorWithSRGBRed:0.80 green:0.64 blue:0.32 alpha:1] setStroke];
        NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(75,75,362,362)];
        ring.lineWidth = 5; [ring stroke];
        NSDictionary *style = @{NSFontAttributeName:[NSFont fontWithName:@"Baskerville" size:290],
            NSForegroundColorAttributeName:[NSColor colorWithSRGBRed:0.94 green:0.82 blue:0.54 alpha:1]};
        NSSize text = [@"II" sizeWithAttributes:style];
        [@"II" drawAtPoint:NSMakePoint((512-text.width)/2,(512-text.height)/2+12) withAttributes:style];
        [NSGraphicsContext restoreGraphicsState];
        NSString *name = [NSString stringWithFormat:@"icon_%d x%d%@.png", size.intValue, size.intValue, scale == 2 ? @"@2x" : @""];
        name = [name stringByReplacingOccurrencesOfString:@" " withString:@""];
        NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![png writeToFile:[folder stringByAppendingPathComponent:name] atomically:YES]) return 1;
      }
    }
  }
  return 0;
}
