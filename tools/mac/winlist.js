ObjC.import('CoreGraphics');
function run() {
  var list = ObjC.deepUnwrap(ObjC.castRefToObject($.CGWindowListCopyWindowInfo($.kCGWindowListOptionAll, 0)));
  return list.filter(function (w) { return (w.kCGWindowOwnerName || '').indexOf('fable') >= 0; })
    .map(function (w) { return [w.kCGWindowNumber, w.kCGWindowLayer, w.kCGWindowIsOnscreen ? 'on' : 'off', w.kCGWindowBounds.Width + 'x' + w.kCGWindowBounds.Height, w.kCGWindowName || ''].join(' '); }).join('\n');
}
