import CoreGraphics
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
for w in list { if let o = w["kCGWindowOwnerName"] as? String, o.contains("PPSSPP") { print(w["kCGWindowNumber"]!); break } }
