package com.nova.engine;

import android.os.Build;
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
    @Test public void rendersEditsSavesAndRestoresAcrossLifecycle() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            waitForFrames();
            String graphics = NativeBridge.INSTANCE.nativeExecConsole("graphics");
            assertTrue(graphics, graphics.contains(Build.VERSION.SDK_INT >= 26 ? "OpenGL ES 3" : "OpenGL ES 2"));
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=1"));
            click(scenario, "addCube");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=2"));
            click(scenario, "undo");
            assertTrue(NativeBridge.INSTANCE.nativeExecConsole("stats").contains("editor_objects=1"));
            click(scenario, "redo");
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
