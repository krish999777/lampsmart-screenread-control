import assert from 'node:assert/strict';
import http from 'node:http';
import test from 'node:test';
import { hsvToRgb, rgbToHsv, rgbToHex, hexToRgb, XLAMP_COLOR_PRESETS } from '../src/utils/color.ts';
import { lampApi, EspApiError } from '../src/services/api.ts';

test('Color conversions round-trip cleanly', () => {
  const red = hsvToRgb(0, 100, 100);
  assert.deepEqual(red, { r: 255, g: 0, b: 0 });
  assert.equal(rgbToHex(red), '#FF0000');

  const green = hsvToRgb(120, 100, 100);
  assert.deepEqual(green, { r: 0, g: 255, b: 0 });

  const blue = hsvToRgb(240, 100, 100);
  assert.deepEqual(blue, { r: 0, g: 0, b: 255 });

  const hsv = rgbToHsv(255, 214, 170);
  const back = hsvToRgb(hsv.h, hsv.s, hsv.v);
  assert.ok(Math.abs(back.r - 255) <= 3);
  assert.ok(Math.abs(back.g - 214) <= 3);
  assert.ok(Math.abs(back.b - 170) <= 3);

  assert.equal(XLAMP_COLOR_PRESETS.length, 12);
  assert.deepEqual(hexToRgb('#FFD9A0'), { r: 255, g: 217, b: 160 });
});

test('API client calls every endpoint with expected JSON and handles ESP32 errors', async () => {
  const requests = [];
  const server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (chunk) => {
      body += chunk;
    });
    req.on('end', () => {
      requests.push({ method: req.method, url: req.url, body: body ? JSON.parse(body) : null });
      res.setHeader('Content-Type', 'application/json');
      if (req.url === '/api/v1/light/brightness' && JSON.parse(body).brightness === 0) {
        res.statusCode = 400;
        res.end(JSON.stringify({ success: false, status: 'invalid_brightness', message: 'brightness must be 1..100' }));
        return;
      }
      if (req.url === '/api/v1/status') {
        res.end(JSON.stringify({ online: true, protocol: { id: 'v3', supportsRgb: true, supportsScenes: true } }));
        return;
      }
      if (req.url === '/api/v1/state') {
        res.end(JSON.stringify({ pushOnly: true, power: true, brightness: 80, temperature: 50 }));
        return;
      }
      res.end(JSON.stringify({ success: true, status: 'transmitted', message: 'ok', txCount: 17, frameHex: '020102' }));
    });
  });

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const addr = server.address();
  const baseUrl = `http://127.0.0.1:${addr.port}`;

  try {
    await lampApi.getStatus(baseUrl);
    await lampApi.getState(baseUrl);
    await lampApi.pair(baseUrl);
    await lampApi.unpair(baseUrl);
    await lampApi.setPower(true, baseUrl);
    await lampApi.setBrightness(45, baseUrl);
    await lampApi.setTemperature(65, baseUrl);
    await lampApi.setColorHsv(210, 80, 75, baseUrl);
    await lampApi.setScene(2, baseUrl);
    await lampApi.sceneAction('on', baseUrl);
    await lampApi.nightLight(baseUrl);
    await lampApi.updateDeviceConfig({ lightName: 'Bedroom', protocol: 'v3' }, baseUrl);
    await lampApi.testCommand(undefined, baseUrl);

    await assert.rejects(
      () => lampApi.setBrightness(0, baseUrl),
      (err) => err instanceof EspApiError && err.statusCode === 400 && err.apiStatus === 'invalid_brightness'
    );

    assert.equal(requests.length, 14);
    assert.deepEqual(requests[4], { method: 'POST', url: '/api/v1/light/power', body: { on: true } });
    assert.deepEqual(requests[5], { method: 'POST', url: '/api/v1/light/brightness', body: { brightness: 45 } });
    assert.deepEqual(requests[6], { method: 'POST', url: '/api/v1/light/temperature', body: { temperature: 65 } });
    assert.deepEqual(requests[7], { method: 'POST', url: '/api/v1/light/color', body: { h: 210, s: 80, v: 75 } });
    assert.deepEqual(requests[8], { method: 'POST', url: '/api/v1/light/scene', body: { scene: 2 } });
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
});

