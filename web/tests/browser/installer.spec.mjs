import { test, expect } from "@playwright/test";

async function fakeDevice(page, settings = {}) {
  await page.addInitScript((settings) => {
    window.testDevice = { ...settings };
    Object.defineProperty(navigator, "serial", {
      configurable: true,
      value: {
        requestPort: async () => {
          window.testDevice.requests = (window.testDevice.requests || 0) + 1;
          if (window.testDevice.cancel) throw new DOMException("No port selected", "NotFoundError");
          return {};
        },
      },
    });
  }, settings);
}

test("production bundle loads at a GitHub Pages subpath with no runtime CDN requests or script errors", async ({ page }) => {
  const errors = [];
  const external = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("request", (request) => {
    if (!request.url().startsWith("http://127.0.0.1:4173/")) external.push(request.url());
  });
  await page.goto("./");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
  expect(await page.locator("body").evaluate((body) => getComputedStyle(body).backgroundColor)).toBe("rgb(8, 12, 24)");
  await expect(page.locator("#version")).toHaveText("Build 42");
  await expect(page.locator("#connect")).toBeDisabled();
  await expect(page.locator(".screen img")).toBeVisible();
  expect(await page.locator(".screen img").evaluate((image) => image.naturalWidth)).toBe(240);
  expect(errors).toEqual([]);
  expect(external).toEqual([]);
});

test("unsupported browsers get instructions rather than an actionable flash button", async ({ page }) => {
  await page.addInitScript(() => { delete Navigator.prototype.serial; });
  await page.goto("./");
  await expect(page.locator("#browser-help")).toBeVisible();
  await page.locator("#board-check").check();
  await expect(page.locator("#connect")).toBeDisabled();
});

test("a cancelled chooser is retryable without writing anything", async ({ page }) => {
  await fakeDevice(page, { cancel: true });
  await page.goto("./");
  await page.locator("#board-check").check();
  await page.locator("#connect").click();
  await expect(page.locator("#status")).toContainText("No device selected");
  await expect(page.locator("#connect")).toBeEnabled();
});

test("update requires confirmation after connecting, writes only the app, and closes USB", async ({ page }) => {
  await fakeDevice(page);
  await page.goto("mock/");
  await page.locator("#board-check").check();
  await page.locator("#connect").click();
  await expect(page.locator("#status")).toContainText("ORBIT layout matches");
  expect(await page.evaluate(() => window.testDevice.addresses)).toBeUndefined();
  await expect(page.locator('input[value="install"]')).toBeDisabled();
  await page.locator("#write").click();
  await expect(page.locator("#status")).toContainText("You're ready to make music");
  expect(await page.evaluate(() => window.testDevice.addresses)).toEqual([0x10000]);
  expect(await page.evaluate(() => window.testDevice.closed)).toBe(true);
});

test("first installation requires explicit data-loss acknowledgement and writes four images", async ({ page }) => {
  await fakeDevice(page);
  await page.goto("mock/");
  await page.locator('input[value="install"]').check();
  await page.locator("#board-check").check();
  await expect(page.locator("#connect")).toBeDisabled();
  await page.locator("#data-check").check();
  await page.locator("#connect").click();
  await expect(page.locator("#write")).toHaveText("Install ORBIT now");
  await page.locator("#write").click();
  await expect(page.locator("#status")).toContainText("You're ready to make music");
  expect(await page.evaluate(() => window.testDevice.addresses)).toEqual([0, 0x8000, 0xe000, 0x10000]);
});

test("connection errors and unplugged devices stay visible and release USB for retry", async ({ page }) => {
  await fakeDevice(page, { writeError: true });
  await page.goto("mock/");
  await page.locator("#board-check").check();
  await page.locator("#connect").click();
  await page.locator("#write").click();
  await expect(page.locator("#status")).toContainText("Installation did not finish");
  await expect(page.locator("#connect")).toBeEnabled();
  await expect(page.locator("#first-play")).toBeHidden();
  expect(await page.evaluate(() => window.testDevice.closed)).toBe(true);
  expect(await page.evaluate(() => window.testDevice.reset)).toBeUndefined();
});

test("wrong chips are refused before write and can be disconnected safely", async ({ page }) => {
  await fakeDevice(page, { wrongChip: true });
  await page.goto("mock/");
  await page.locator("#board-check").check();
  await page.locator("#connect").click();
  await expect(page.locator("#status")).toContainText("not an ESP32-C6");
  await expect(page.locator("#write")).toBeHidden();
  expect(await page.evaluate(() => window.testDevice.addresses)).toBeUndefined();
  expect(await page.evaluate(() => window.testDevice.closed)).toBe(true);
});

test("a missing publication shows a useful error and retry control", async ({ page }) => {
  await page.route("**/firmware/manifest.json", (route) => route.fulfill({ status: 404, body: "Not published" }));
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("Could not load the latest firmware");
  await expect(page.locator("#reload")).toBeVisible();
  await page.locator("#board-check").check();
  await expect(page.locator("#connect")).toBeDisabled();
});

test("mobile layout and both themes keep the installer within the viewport", async ({ page }) => {
  await page.setViewportSize({ width: 375, height: 812 });
  for (const theme of ["light", "dark"]) {
    await page.goto(`./?scoutTheme=${theme}`);
    await expect(page.locator("html")).toHaveAttribute("data-theme", theme);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    const bounds = await page.locator("#connect").boundingBox();
    expect(bounds.height).toBeGreaterThanOrEqual(44);
    expect(bounds.x + bounds.width).toBeLessThanOrEqual(375);
  }
});
