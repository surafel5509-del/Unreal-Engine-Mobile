package com.nova.engine;

import android.os.Build;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Rect;
import android.widget.EditText;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.FileOutputStream;
import android.view.View;
import androidx.lifecycle.Lifecycle;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.io.File;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class EditorIntegrationTest {
    private long frames() {
        Matcher match = Pattern.compile("frames=(\\d+)").matcher(NativeBridge.INSTANCE.nativeExecConsole("stats"));
        assertTrue("No native frame stats", match.find());
        return Long.parseLong(match.group(1));
    }
    private void waitForFrames() throws Exception {
        long before = frames();
        long deadline = System.currentTimeMillis() + 15000;
        while (System.currentTimeMillis() < deadline && frames() <= before + 3) Thread.sleep(100);
        assertTrue("Native renderer did not produce frames", frames() > before + 3);
    }
    private void click(ActivityScenario<MainActivity> scenario, String tag) {
        scenario.onActivity(activity -> {
            View button = activity.getWindow().getDecorView().findViewWithTag(tag);
            assertNotNull("Missing editor control: " + tag, button);
            assertTrue(button.performClick());
        });
    }
    private void assertViewportPixels(ActivityScenario<MainActivity> scenario) throws Exception {
        Rect bounds = new Rect();
        scenario.onActivity(activity -> {
            View viewport = activity.getWindow().getDecorView().findViewWithTag("viewport");
            assertNotNull(viewport);
            assertTrue(viewport.getGlobalVisibleRect(bounds));
        });
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        Bitmap bitmap = null;
        int coloredPixels = 0;
        long deadline = System.currentTimeMillis() + 10000;
        // A rendered native frame can precede the first SurfaceFlinger composite
        // during an Activity transition, especially on the API-21 emulator.
        do {
            if (bitmap != null) bitmap.recycle();
            bitmap = InstrumentationRegistry.getInstrumentation().getUiAutomation().takeScreenshot();
            assertNotNull("Screenshot unavailable", bitmap);
            coloredPixels = 0;
            for (int y = bounds.top + bounds.height() / 5; y < bounds.bottom - bounds.height() / 5; y += 3) {
                for (int x = bounds.left + bounds.width() / 5; x < bounds.right - bounds.width() / 5; x += 3) {
                    int pixel = bitmap.getPixel(x, y);
                    if (Color.blue(pixel) > 100 && Color.green(pixel) > 60 && Color.blue(pixel) > Color.red(pixel) * 1.1) coloredPixels++;
                }
            }
            if (coloredPixels > 50) break;
            Thread.sleep(250);
        } while (System.currentTimeMillis() < deadline);
        File screenshot = new File(InstrumentationRegistry.getInstrumentation().getTargetContext().getFilesDir(), "editor-tested.png");
        try (FileOutputStream output = new FileOutputStream(screenshot)) { bitmap.compress(Bitmap.CompressFormat.PNG, 100, output); }
        String detail = "pixels=" + coloredPixels + " viewport=" + bounds + " bitmap=" + bitmap.getWidth() + "x" + bitmap.getHeight()
            + " center=" + Integer.toHexString(bitmap.getPixel(bounds.centerX(), bounds.centerY()))
            + " stats=" + NativeBridge.INSTANCE.nativeExecConsole("stats");
        bitmap.recycle();
        assertTrue("Viewport contains no visible cube pixels: " + detail, coloredPixels > 50);
    }
    @Test public void rendersEditsSavesAndRestoresAcrossLifecycle() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            waitForFrames();
            assertViewportPixels(scenario);
            String graphics = NativeBridge.INSTANCE.nativeExecConsole("graphics");
            assertTrue(graphics, graphics.contains(Build.VERSION.SDK_INT >= 26 ? "OpenGL ES 3" : "OpenGL ES 2"));
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=1"));
            click(scenario, "addCube");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=2"));
            click(scenario, "undo");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=1"));
            click(scenario, "redo");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=2"));
            click(scenario, "object_1");
            scenario.onActivity(activity -> {
                EditText x = activity.getWindow().getDecorView().findViewWithTag("field_x");
                assertNotNull(x); x.setText("0.2");
            });
            click(scenario, "apply");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("scene").contains("position=0.2,"));
            click(scenario, "duplicate");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=3"));
            click(scenario, "delete");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=2"));
            click(scenario, "play");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("playing=yes"));
            waitForFrames();
            click(scenario, "play");
            click(scenario, "save");
            scenario.onActivity(activity -> assertTrue(new File(activity.getFilesDir(), "editor/scene.json").isFile()));
            String invalid = NativeBridge.INSTANCE.nativeSetScene("{\"version\":1,\"objects\":[{}]}");
            assertTrue(invalid.startsWith("error:"));
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=2"));
            scenario.moveToState(Lifecycle.State.CREATED);
            scenario.moveToState(Lifecycle.State.RESUMED);
            waitForFrames();
            scenario.recreate();
            waitForFrames();
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=2"));
        }
    }
}
