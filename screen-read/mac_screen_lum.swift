import Foundation
import CoreGraphics
import ScreenCaptureKit

@main
struct MacScreenLum {
    static func computeLuminance(image: CGImage) -> Double {
        let w = 64
        let h = 64
        var pixels = [UInt8](repeating: 0, count: w * h * 4)
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue

        guard let ctx = CGContext(
            data: &pixels,
            width: w,
            height: h,
            bitsPerComponent: 8,
            bytesPerRow: w * 4,
            space: colorSpace,
            bitmapInfo: bitmapInfo
        ) else {
            return -1.0
        }

        ctx.interpolationQuality = .low
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))

        var sum = 0.0
        let count = w * h
        var i = 0
        while i < count * 4 {
            let r = Double(pixels[i])
            let g = Double(pixels[i + 1])
            let b = Double(pixels[i + 2])
            sum += (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            i += 4
        }
        return (sum / Double(count)) * 100.0
    }

    static func main() async {
        let args = CommandLine.arguments
        let displayIndex = (args.count > 1 ? Int(args[1]) : nil) ?? 1

        var cachedFilter: SCContentFilter?
        let config = SCStreamConfiguration()
        config.width = 64
        config.height = 64
        config.showsCursor = false
        config.scalesToFit = true

        while let line = readLine() {
            let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed == "quit" { break }

            do {
                if cachedFilter == nil {
                    let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
                    guard !content.displays.isEmpty else {
                        print("ERR:no_displays")
                        fflush(stdout)
                        continue
                    }
                    let idx = max(0, min(content.displays.count - 1, displayIndex - 1))
                    let display = content.displays[idx]
                    cachedFilter = SCContentFilter(display: display, excludingWindows: [])
                }

                guard let filter = cachedFilter else {
                    print("ERR:no_filter")
                    fflush(stdout)
                    continue
                }

                let cgImg = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
                let lum = computeLuminance(image: cgImg)
                if lum < 0 {
                    print("ERR:cgcontext_failed")
                } else {
                    print(String(format: "OK:%.2f,64,64", lum))
                }
            } catch {
                cachedFilter = nil
                print("ERR:\(error.localizedDescription)")
            }
            fflush(stdout)
        }
    }
}

