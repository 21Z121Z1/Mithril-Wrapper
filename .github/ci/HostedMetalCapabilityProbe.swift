import Foundation
import Metal

enum ProbeError: Error, CustomStringConvertible {
    case message(String)

    var description: String {
        switch self {
        case .message(let value): return value
        }
    }
}

@inline(__always)
func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw ProbeError.message(message) }
}

let source = """
#include <metal_stdlib>
using namespace metal;

kernel void hosted_metal_probe(
    device uint *output [[buffer(0)]],
    uint gid [[thread_position_in_grid]]) {
    if (gid < 256) {
        output[gid] = gid * 3u + 7u;
    }
}
"""

do {
    print("CI=\(ProcessInfo.processInfo.environment["CI"] ?? "<unset>")")
    guard let device = MTLCreateSystemDefaultDevice() else {
        throw ProbeError.message("MTLCreateSystemDefaultDevice returned nil")
    }
#if os(macOS)
    print("MTLDevice name=\(device.name) registryID=\(device.registryID) lowPower=\(device.isLowPower) removable=\(device.isRemovable)")
#else
    print("MTLDevice name=\(device.name) registryID=\(device.registryID)")
#endif

    guard let queue = device.makeCommandQueue() else {
        throw ProbeError.message("MTLDevice.makeCommandQueue returned nil")
    }

    let library: MTLLibrary
    do {
        library = try device.makeLibrary(source: source, options: nil)
    } catch {
        throw ProbeError.message("MSL compilation failed: \(error)")
    }
    guard let function = library.makeFunction(name: "hosted_metal_probe") else {
        throw ProbeError.message("MSL function hosted_metal_probe was not found")
    }
    let pipeline: MTLComputePipelineState
    do {
        pipeline = try device.makeComputePipelineState(function: function)
    } catch {
        throw ProbeError.message("compute pipeline creation failed: \(error)")
    }

    let elementCount = 256
    let byteCount = elementCount * MemoryLayout<UInt32>.stride
    guard let buffer = device.makeBuffer(length: byteCount, options: .storageModeShared) else {
        throw ProbeError.message("shared MTLBuffer allocation failed")
    }
    buffer.label = "GitHub hosted Metal capability probe output"
    let values = buffer.contents().bindMemory(to: UInt32.self, capacity: elementCount)
    for i in 0..<elementCount { values[i] = 0xDEADBEEF }

    guard let commandBuffer = queue.makeCommandBuffer() else {
        throw ProbeError.message("MTLCommandQueue.makeCommandBuffer returned nil")
    }
    commandBuffer.label = "GitHub hosted Metal capability probe"
    guard let encoder = commandBuffer.makeComputeCommandEncoder() else {
        throw ProbeError.message("makeComputeCommandEncoder returned nil")
    }
    encoder.setComputePipelineState(pipeline)
    encoder.setBuffer(buffer, offset: 0, index: 0)
    let width = max(1, min(64, pipeline.maxTotalThreadsPerThreadgroup))
    encoder.dispatchThreads(
        MTLSize(width: elementCount, height: 1, depth: 1),
        threadsPerThreadgroup: MTLSize(width: width, height: 1, depth: 1)
    )
    encoder.endEncoding()

    commandBuffer.commit()
    commandBuffer.waitUntilCompleted()
    print("MTLCommandBuffer status=\(commandBuffer.status.rawValue) error=\(String(describing: commandBuffer.error)) gpuStart=\(commandBuffer.gpuStartTime) gpuEnd=\(commandBuffer.gpuEndTime)")
    try require(commandBuffer.status == .completed, "command buffer did not complete: \(String(describing: commandBuffer.error))")
    try require(commandBuffer.error == nil, "command buffer completed with error: \(String(describing: commandBuffer.error))")

    for i in 0..<elementCount {
        let expected = UInt32(i * 3 + 7)
        if values[i] != expected {
            throw ProbeError.message("GPU readback mismatch at index \(i): expected \(expected), got \(values[i])")
        }
    }

    print("HOSTED_METAL_GPU_PROBE_PASS device=\(device.name) elements=\(elementCount) formula=i*3+7")
} catch {
    fputs("HOSTED_METAL_GPU_PROBE_FAIL: \(error)\n", stderr)
    exit(1)
}
