/* Client-side GIF normalization. The robot serves this file and stores only
 * the bounded output; decoding and palette work happen in the user's browser. */
const MAX_SOURCE_BYTES = 10 * 1024 * 1024;
const MAX_SOURCE_DIMENSION = 512;
const MAX_SOURCE_FRAMES = 120;
const MAX_OUTPUT_BYTES = 256 * 1024;
const OUTPUT_FPS = 10;

function fillRgba(pixels, color) {
  const r = parseInt(color.slice(1, 3), 16);
  const g = parseInt(color.slice(3, 5), 16);
  const b = parseInt(color.slice(5, 7), 16);
  for (let i = 0; i < pixels.length; i += 4) {
    pixels[i] = r;
    pixels[i + 1] = g;
    pixels[i + 2] = b;
    pixels[i + 3] = 255;
  }
}

function resizeContain(source, sourceWidth, sourceHeight, targetSize, background) {
  const output = new Uint8ClampedArray(targetSize * targetSize * 4);
  fillRgba(output, background);
  const scale = Math.min(targetSize / sourceWidth, targetSize / sourceHeight);
  const drawWidth = Math.max(1, Math.round(sourceWidth * scale));
  const drawHeight = Math.max(1, Math.round(sourceHeight * scale));
  const left = Math.floor((targetSize - drawWidth) / 2);
  const top = Math.floor((targetSize - drawHeight) / 2);
  for (let y = 0; y < drawHeight; y++) {
    const sourceY = Math.max(0, Math.min(sourceHeight - 1,
      (y + 0.5) * sourceHeight / drawHeight - 0.5));
    const y0 = Math.floor(sourceY);
    const y1 = Math.min(sourceHeight - 1, y0 + 1);
    const fy = sourceY - y0;
    for (let x = 0; x < drawWidth; x++) {
      const sourceX = Math.max(0, Math.min(sourceWidth - 1,
        (x + 0.5) * sourceWidth / drawWidth - 0.5));
      const x0 = Math.floor(sourceX);
      const x1 = Math.min(sourceWidth - 1, x0 + 1);
      const fx = sourceX - x0;
      const from00 = (y0 * sourceWidth + x0) * 4;
      const from10 = (y0 * sourceWidth + x1) * 4;
      const from01 = (y1 * sourceWidth + x0) * 4;
      const from11 = (y1 * sourceWidth + x1) * 4;
      const to = ((top + y) * targetSize + left + x) * 4;
      for (let channel = 0; channel < 3; channel++) {
        const upper = source[from00 + channel] * (1 - fx) + source[from10 + channel] * fx;
        const lower = source[from01 + channel] * (1 - fx) + source[from11 + channel] * fx;
        output[to + channel] = Math.round(upper * (1 - fy) + lower * fy);
      }
      output[to + 3] = 255;
    }
  }
  return output;
}

function clearPreviousFrame(canvas, frame, background, restore, canvasWidth) {
  if (!frame) return;
  if (frame.disposal === 3 && restore) {
    canvas.set(restore);
  } else if (frame.disposal === 2) {
    const color = [
      parseInt(background.slice(1, 3), 16),
      parseInt(background.slice(3, 5), 16),
      parseInt(background.slice(5, 7), 16),
      255
    ];
    const xEnd = frame.x + frame.width;
    const yEnd = frame.y + frame.height;
    for (let y = frame.y; y < yEnd; y++) {
      for (let x = frame.x; x < xEnd; x++) {
        const offset = (y * canvasWidth + x) * 4;
        canvas[offset] = color[0];
        canvas[offset + 1] = color[1];
        canvas[offset + 2] = color[2];
        canvas[offset + 3] = 255;
      }
    }
  }
}

function decodeFrames(bytes, background, onProgress) {
  if (bytes.byteLength < 14 || bytes.byteLength > MAX_SOURCE_BYTES)
    throw new Error('GIF nguồn phải nhỏ hơn 10 MiB.');
  const reader = new exports.GifReader(bytes);
  const width = reader.width;
  const height = reader.height;
  const count = reader.numFrames();
  if (!width || !height || width > MAX_SOURCE_DIMENSION || height > MAX_SOURCE_DIMENSION)
    throw new Error('Kích thước GIF nguồn tối đa là 512×512 pixel.');
  if (!count || count > MAX_SOURCE_FRAMES)
    throw new Error('GIF nguồn cần có từ 1 đến 120 khung hình.');

  const targetCount = Math.min(40, count);
  const targets = new Set();
  for (let i = 0; i < targetCount; i++)
    targets.add(Math.floor(i * count / targetCount));

  const canvas = new Uint8ClampedArray(width * height * 4);
  const decoded = new Uint8ClampedArray(width * height * 4);
  fillRgba(canvas, background);
  const frames = [];
  let previous = null;
  let restore = null;
  for (let index = 0; index < count; index++) {
    clearPreviousFrame(canvas, previous, background, restore, width);
    const info = reader.frameInfo(index);
    if (!info.width || !info.height || info.x < 0 || info.y < 0 ||
        info.x + info.width > width || info.y + info.height > height)
      throw new Error('GIF chứa khung hình vượt khỏi kích thước ảnh.');
    const priorCanvas = info.disposal === 3 ? canvas.slice() : null;
    decoded.fill(0);
    reader.decodeAndBlitFrameRGBA(index, decoded);
    for (let y = info.y; y < info.y + info.height; y++) {
      for (let x = info.x; x < info.x + info.width; x++) {
        const offset = (y * width + x) * 4;
        const alpha = decoded[offset + 3];
        if (alpha === 255) {
          canvas[offset] = decoded[offset];
          canvas[offset + 1] = decoded[offset + 1];
          canvas[offset + 2] = decoded[offset + 2];
        } else if (alpha > 0) {
          const inverse = 255 - alpha;
          canvas[offset] = Math.round((decoded[offset] * alpha + canvas[offset] * inverse) / 255);
          canvas[offset + 1] = Math.round((decoded[offset + 1] * alpha + canvas[offset + 1] * inverse) / 255);
          canvas[offset + 2] = Math.round((decoded[offset + 2] * alpha + canvas[offset + 2] * inverse) / 255);
        }
        canvas[offset + 3] = 255;
      }
    }
    if (targets.has(index))
      frames.push(resizeContain(canvas, width, height, 160, background));
    previous = info;
    restore = priorCanvas;
    if (index % 4 === 0 || index + 1 === count)
      onProgress(Math.round((index + 1) * 55 / count));
  }
  return frames;
}

function resizeSquare(source, sourceSize, targetSize) {
  if (sourceSize === targetSize) return source;
  const output = new Uint8ClampedArray(targetSize * targetSize * 4);
  for (let y = 0; y < targetSize; y++) {
    const sourceY = Math.max(0, Math.min(sourceSize - 1,
      (y + 0.5) * sourceSize / targetSize - 0.5));
    const y0 = Math.floor(sourceY);
    const y1 = Math.min(sourceSize - 1, y0 + 1);
    const fy = sourceY - y0;
    for (let x = 0; x < targetSize; x++) {
      const sourceX = Math.max(0, Math.min(sourceSize - 1,
        (x + 0.5) * sourceSize / targetSize - 0.5));
      const x0 = Math.floor(sourceX);
      const x1 = Math.min(sourceSize - 1, x0 + 1);
      const fx = sourceX - x0;
      const from00 = (y0 * sourceSize + x0) * 4;
      const from10 = (y0 * sourceSize + x1) * 4;
      const from01 = (y1 * sourceSize + x0) * 4;
      const from11 = (y1 * sourceSize + x1) * 4;
      const to = (y * targetSize + x) * 4;
      for (let channel = 0; channel < 3; channel++) {
        const upper = source[from00 + channel] * (1 - fx) + source[from10 + channel] * fx;
        const lower = source[from01 + channel] * (1 - fx) + source[from11 + channel] * fx;
        output[to + channel] = Math.round(upper * (1 - fy) + lower * fy);
      }
      output[to + 3] = 255;
    }
  }
  return output;
}

function encodeFrames(frames, profile, onProgress, progressBase, progressSpan) {
  const selected = frames.length > profile.frames
    ? Array.from({length: profile.frames}, (_, i) => frames[Math.floor(i * frames.length / profile.frames)])
    : frames;
  const size = profile.size;
  const sample = new Uint8Array(selected.length * 20 * 20 * 4);
  let sampleOffset = 0;
  for (const frame of selected) {
    for (let y = 0; y < 160; y += 8) {
      for (let x = 0; x < 160; x += 8) {
        const sourceOffset = (y * 160 + x) * 4;
        sample[sampleOffset++] = frame[sourceOffset];
        sample[sampleOffset++] = frame[sourceOffset + 1];
        sample[sampleOffset++] = frame[sourceOffset + 2];
        sample[sampleOffset++] = 255;
      }
    }
  }
  const palette = exports.quantize(sample, profile.colors, {format: 'rgb444'});
  const encoder = exports.GIFEncoder({initialCapacity: 16 * 1024});
  for (let index = 0; index < selected.length; index++) {
    const resized = resizeSquare(selected[index], 160, size);
    const indexed = exports.applyPalette(resized, palette, 'rgb444');
    encoder.writeFrame(indexed, size, size, {
      palette,
      delay: Math.round(1000 / OUTPUT_FPS),
      repeat: 0,
      dispose: 1
    });
    if (index % 4 === 0 || index + 1 === selected.length)
      onProgress(progressBase + Math.round((index + 1) * progressSpan / selected.length));
  }
  encoder.finish();
  return encoder.bytes();
}

function normalize(buffer, background, onProgress) {
  const frames = decodeFrames(new Uint8Array(buffer), background, onProgress);
  const profiles = [
    {size: 160, frames: 40, colors: 128},
    {size: 160, frames: 30, colors: 96},
    {size: 128, frames: 24, colors: 64},
    {size: 96, frames: 16, colors: 48},
    {size: 80, frames: 12, colors: 32}
  ];
  for (let index = 0; index < profiles.length; index++) {
    const profile = profiles[index];
    const base = 55 + Math.round(index * 45 / profiles.length);
    const end = 55 + Math.round((index + 1) * 45 / profiles.length);
    const encoded = encodeFrames(frames, profile, onProgress, base, end - base);
    if (encoded.byteLength <= MAX_OUTPUT_BYTES)
      return {bytes: encoded, width: profile.size, height: profile.size, frames: Math.min(frames.length, profile.frames)};
  }
  throw new Error('GIF vẫn vượt 256 KiB sau khi giảm độ phân giải và màu. Hãy chọn GIF đơn giản hơn.');
}

self.onmessage = event => {
  const {id, buffer, background} = event.data || {};
  try {
    if (!(buffer instanceof ArrayBuffer)) throw new Error('Không đọc được file GIF.');
    if (!/^#[0-9a-f]{6}$/i.test(background || '')) throw new Error('Màu nền không hợp lệ.');
    const result = normalize(buffer, background, progress => self.postMessage({type: 'progress', id, progress}));
    const output = result.bytes.buffer.slice(result.bytes.byteOffset, result.bytes.byteOffset + result.bytes.byteLength);
    self.postMessage({
      type: 'done', id, buffer: output, width: result.width, height: result.height, frames: result.frames
    }, [output]);
  } catch (error) {
    self.postMessage({type: 'error', id, error: error && error.message ? error.message : 'GIF không hợp lệ.'});
  }
};
