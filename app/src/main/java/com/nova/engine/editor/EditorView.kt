package com.nova.engine.editor

import android.app.AlertDialog
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.util.AtomicFile
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.*
import com.nova.engine.NativeBridge
import com.nova.engine.NovaSurfaceView
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.ArrayDeque
import kotlin.math.max

/** Small scene editor, backed by the native ECS and renderer (not a mock viewport). */
class EditorView(context: Context, private val viewport: NovaSurfaceView) : LinearLayout(context) {
    private val bg = Color.rgb(18, 21, 28)
    private val panel = Color.rgb(26, 30, 39)
    private val line = Color.rgb(44, 50, 63)
    private val ink = Color.rgb(225, 231, 241)
    private val muted = Color.rgb(145, 157, 180)
    private val accent = Color.rgb(132, 150, 255)
    private var scene = defaultScene()
    private var selected = 0
    private var playing = false
    private var dirty = false
    private var tab = "Hierarchy"
    private val undo = ArrayDeque<String>()
    private val redo = ArrayDeque<String>()
    private var hierarchy: LinearLayout? = null
    private var inspector: LinearLayout? = null
    private var bottom: LinearLayout? = null
    private var status: TextView? = null
    private var logView: TextView? = null
    private var playButton: Button? = null
    private var titleView: TextView? = null
    private var zoom = 3f
    private val logs = ArrayDeque<String>()
    private val sceneFile = AtomicFile(File(context.filesDir, "editor/scene.json"))
    private val recoveryFile = AtomicFile(File(context.filesDir, "editor/recovery.json"))
    private var attached = false
    private val statsTick = object : Runnable {
        override fun run() {
            if (!attached) return
            val stats = NativeBridge.nativeExecConsole("stats")
            val fps = Regex("fps=([0-9.]+)").find(stats)?.groupValues?.get(1)?.toFloatOrNull()?.toInt() ?: 0
            status?.text = "  ${objects().length()} / 32 objects    •    $fps FPS    •    ${if (playing) "PREVIEW" else "EDIT MODE"}"
            postDelayed(this, 1000)
        }
    }

    init {
        orientation = VERTICAL
        setBackgroundColor(bg)
        try {
            val file = if (recoveryFile.baseFile.exists()) recoveryFile else sceneFile
            if (file.baseFile.exists()) scene = readScene(file)
        } catch (e: Exception) { log("Could not restore scene: ${e.message}") }
        val result = NativeBridge.nativeSetScene(scene.toString())
        if (result != "ok") {
            log("Saved scene rejected: $result")
            scene = defaultScene()
            NativeBridge.nativeSetScene(scene.toString())
        }
        log("Ready • " + NativeBridge.nativeExecConsole("graphics"))
        installDragControls()
        rebuild()
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    private fun objects(): JSONArray = scene.getJSONArray("objects")
    private fun selectedObject(): JSONObject? = objects().optJSONObject(selected)
    private fun text(value: String, size: Float = 12f, color: Int = ink) = TextView(context).apply {
        text = value; textSize = size; setTextColor(color); gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(10), dp(6), dp(10), dp(6))
    }
    private fun column() = LinearLayout(context).apply { orientation = VERTICAL; setBackgroundColor(panel) }
    private fun row() = LinearLayout(context).apply { orientation = HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
    private fun shape(color: Int) = GradientDrawable().apply {
        setColor(color); cornerRadius = dp(5).toFloat(); setStroke(dp(1), line)
    }
    private fun button(label: String, key: String = label, action: () -> Unit) = Button(context).apply {
        text = label; tag = key; contentDescription = label; textSize = 11f; isAllCaps = false
        setTextColor(ink); background = shape(line); minWidth = 0; minimumWidth = 0
        minHeight = 0; minimumHeight = 0; setPadding(dp(12), 0, dp(12), 0)
        layoutParams = LayoutParams(LayoutParams.WRAP_CONTENT, dp(40)).apply { setMargins(dp(3), dp(3), dp(3), dp(3)) }
        setOnClickListener { action() }
    }
    private fun section(parent: LinearLayout, name: String) {
        parent.addView(text(name.uppercase(), 10f, muted).apply { typeface = Typeface.DEFAULT_BOLD }, LayoutParams(-1, dp(34)))
    }
    private fun scroll(child: View) = ScrollView(context).apply { isFillViewport = true; addView(child) }
    private fun toolbarScroll(child: View) = HorizontalScrollView(context).apply {
        isHorizontalScrollBarEnabled = false; addView(child)
    }

    fun rebuild() {
        (viewport.parent as? ViewGroup)?.removeView(viewport)
        removeAllViews()
        hierarchy = null; inspector = null; bottom = null; logView = null
        val header = row().apply { setBackgroundColor(panel) }
        header.addView(text("N  NOVA STUDIO", 14f, accent).apply { typeface = Typeface.DEFAULT_BOLD })
        titleView = text("Scene${if (dirty) " •" else ""}", 12f, muted)
        header.addView(titleView, LayoutParams(0, dp(46), 1f))
        header.addView(button("Save", "save") { save() })
        addView(header, LayoutParams(-1, dp(50)))
        val toolbar = row()
        toolbar.addView(button("+ Object", "add") { showAddMenu() })
        toolbar.addView(button("Undo", "undo") { historyBack() })
        toolbar.addView(button("Redo", "redo") { historyForward() })
        playButton = button(if (playing) "■ Stop" else "▶ Play", "play") { togglePlay() }
        playButton?.setTextColor(accent)
        toolbar.addView(playButton)
        toolbar.addView(button("Load", "load") { confirmLoad() })
        toolbar.addView(button("New", "new") {
            AlertDialog.Builder(context).setTitle("New scene?").setMessage("The current scene can be restored with Undo.")
                .setPositiveButton("New") { _, _ -> replace(defaultScene(), true) }.setNegativeButton("Cancel", null).show()
        })
        addView(toolbarScroll(toolbar), LayoutParams(-1, dp(48)))

        val center = row()
        val wide = resources.configuration.screenWidthDp >= 700
        if (wide) {
            val left = column()
            section(left, "Hierarchy")
            hierarchy = column(); left.addView(scroll(hierarchy!!), LayoutParams(-1, 0, 1f))
            left.addView(button("+ Cube", "addCube") { addObject("Cube") })
            left.addView(button("+ Triangle", "addTriangle") { addObject("Triangle") })
            center.addView(left, LayoutParams(dp(155), -1))
        }
        val stage = column()
        val stageHeader = row()
        stageHeader.addView(text("SCENE  /  ${if (playing) "PREVIEW" else "PERSPECTIVE"}", 10f, muted), LayoutParams(0, dp(38), 1f))
        stageHeader.addView(button("−", "zoomOut") { cameraZoom(zoom + 0.5f) })
        stageHeader.addView(button("+", "zoomIn") { cameraZoom(zoom - 0.5f) })
        stage.addView(stageHeader)
        val frame = FrameLayout(context)
        frame.addView(viewport, FrameLayout.LayoutParams(-1, -1))
        frame.addView(text("Y ↑   X →", 11f, accent).apply { setBackgroundColor(0x99202530.toInt()) },
            FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END).apply { setMargins(0, dp(8), dp(8), 0) })
        stage.addView(frame, LayoutParams(-1, 0, 1f))
        stage.addView(text("Drag selected object to move  •  + / − to zoom", 10f, muted), LayoutParams(-1, dp(28)))
        center.addView(stage, LayoutParams(0, -1, 1f))
        if (wide) {
            val right = column()
            val tabs = row()
            for (name in listOf("Inspector", "Console")) tabs.addView(button(name) { tab = name; rebuild() })
            right.addView(tabs)
            if (tab == "Console") right.addView(consolePanel(), LayoutParams(-1, 0, 1f))
            else { inspector = column(); right.addView(scroll(inspector!!), LayoutParams(-1, 0, 1f)) }
            center.addView(right, LayoutParams(dp(245), -1))
        }
        addView(center, LayoutParams(-1, 0, 1f))
        if (!wide) {
            val dock = column()
            val tabs = row()
            for (name in listOf("Hierarchy", "Inspector", "Assets", "Console")) {
                val b = button(name) { tab = name; refreshDock() }
                b.layoutParams = LayoutParams(0, dp(42), 1f)
                tabs.addView(b)
            }
            dock.addView(tabs)
            bottom = column(); dock.addView(bottom, LayoutParams(-1, 0, 1f))
            addView(dock, LayoutParams(-1, dp(if (resources.configuration.screenHeightDp < 600) 190 else 245)))
            refreshDock()
        }
        status = text("Starting viewport…", 10f, muted)
        addView(status, LayoutParams(-1, dp(28)))
        refreshHierarchy(); refreshInspector()
    }

    private fun refreshDock() {
        val host = bottom ?: return
        host.removeAllViews(); hierarchy = null; inspector = null; logView = null
        when (tab) {
            "Hierarchy" -> {
                hierarchy = column(); host.addView(scroll(hierarchy!!), LayoutParams(-1, -1)); refreshHierarchy()
            }
            "Inspector" -> {
                inspector = column(); host.addView(scroll(inspector!!), LayoutParams(-1, -1)); refreshInspector()
            }
            "Assets" -> {
                section(host, "Built-in mesh library")
                val meshes = row()
                meshes.addView(button("▣ Cube", "addCube") { addObject("Cube") })
                meshes.addView(button("△ Triangle", "addTriangle") { addObject("Triangle") })
                host.addView(meshes)
                host.addView(text("Tap a mesh to add it to the scene.\nExternal model import is not available in this version.", 12f, muted))
            }
            else -> host.addView(consolePanel(), LayoutParams(-1, -1))
        }
    }

    private fun refreshHierarchy() {
        val host = hierarchy ?: return
        host.removeAllViews()
        host.addView(text("▾  Scene  (${objects().length()} objects)", 12f, muted))
        host.addView(text("    ◉  Camera  ·  Perspective", 11f, muted))
        for (i in 0 until objects().length()) {
            val obj = objects().getJSONObject(i)
            val entry = text("    ${if (obj.getString("kind") == "Cube") "▣" else "△"}  ${obj.optString("name", obj.getString("kind"))}", 13f)
            entry.tag = "object_$i"
            entry.setBackgroundColor(if (i == selected) Color.rgb(49, 60, 91) else panel)
            entry.layoutParams = LayoutParams(-1, dp(42))
            entry.setOnClickListener { selected = i; refreshHierarchy(); tab = "Inspector"; refreshDock(); refreshInspector() }
            host.addView(entry)
        }
        val actions = row()
        actions.addView(button("+ Cube", "addCube") { addObject("Cube") })
        actions.addView(button("+ Triangle", "addTriangle") { addObject("Triangle") })
        host.addView(toolbarScroll(actions))
    }

    private fun refreshInspector() {
        val host = inspector ?: return
        host.removeAllViews()
        val obj = selectedObject()
        if (obj == null) { host.addView(text("Select or add an object.", 13f, muted)); return }
        section(host, "${obj.optString("name", obj.getString("kind"))}  /  Transform")
        val fields = linkedMapOf<String, EditText>()
        for ((label, keys) in listOf("Position" to listOf("x", "y", "z"), "Rotation °" to listOf("rx", "ry", "rz"), "Scale" to listOf("scale"))) {
            host.addView(text(label, 11f, muted))
            val inputs = row()
            for (key in keys) {
                inputs.addView(text(key.removePrefix("r").uppercase(), 10f, muted))
                val input = EditText(context).apply {
                    setText(obj.getDouble(key).toString()); textSize = 12f; setTextColor(ink)
                    inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL or InputType.TYPE_NUMBER_FLAG_SIGNED
                    setSingleLine(); tag = "field_$key"; setSelectAllOnFocus(true)
                    contentDescription = "$label $key"
                    setPadding(dp(3), 0, dp(3), 0)
                }
                fields[key] = input
                inputs.addView(input, LayoutParams(0, dp(42), 1f))
            }
            host.addView(inputs)
        }
        val actions = row()
        actions.addView(button("Apply", "apply") {
            val changed = JSONObject(scene.toString())
            val target = changed.getJSONArray("objects").getJSONObject(selected)
            for ((key, input) in fields) {
                val number = input.text.toString().toDoubleOrNull()
                if (number == null || number.isNaN() || number.isInfinite()) { toast("Enter a valid number for $key"); return@button }
                target.put(key, number)
            }
            replace(changed, true)
        })
        actions.addView(button("Copy", "duplicate") { duplicate() })
        actions.addView(button("Delete", "delete") { deleteObject() })
        host.addView(toolbarScroll(actions))
        section(host, "Material tint")
        val colors = row()
        for ((label, rgb) in listOf("White" to 0xffffff, "Blue" to 0x819cff, "Mint" to 0x4de3b4, "Gold" to 0xffc66b, "Rose" to 0xff84ad)) {
            val b = button("●", "color_$label") {
                val changed = JSONObject(scene.toString())
                changed.getJSONArray("objects").getJSONObject(selected).apply {
                    put("r", ((rgb shr 16) and 255) / 255.0); put("g", ((rgb shr 8) and 255) / 255.0); put("b", (rgb and 255) / 255.0)
                }
                replace(changed, true)
            }
            b.setTextColor(rgb or 0xff000000.toInt()); b.contentDescription = "$label material"
            colors.addView(b)
        }
        host.addView(toolbarScroll(colors))
    }

    private fun consolePanel(): View {
        val host = column()
        logView = text(logs.joinToString("\n"), 11f, muted).apply { typeface = Typeface.MONOSPACE; setTextIsSelectable(true) }
        host.addView(scroll(logView!!), LayoutParams(-1, 0, 1f))
        val controls = row()
        val command = EditText(context).apply {
            setTextColor(ink); setHintTextColor(muted); hint = "help / stats / graphics"; textSize = 12f; setSingleLine(); tag = "consoleInput"
        }
        controls.addView(command, LayoutParams(0, dp(44), 1f))
        controls.addView(button("Run", "consoleRun") {
            val input = command.text.toString().trim()
            if (input == "play") { if (!playing) togglePlay() }
            else if (input == "stop") { if (playing) togglePlay() }
            else if (input.isNotEmpty()) log("> $input\n" + NativeBridge.nativeExecConsole(input))
            command.setText("")
        })
        host.addView(controls)
        return host
    }

    private fun showAddMenu() {
        AlertDialog.Builder(context).setTitle("Add to scene").setItems(arrayOf("Cube", "Triangle")) { _, which ->
            addObject(if (which == 0) "Cube" else "Triangle")
        }.show()
    }
    private fun addObject(kind: String) {
        if (objects().length() >= 32) { toast("This version supports 32 objects per scene."); return }
        val changed = JSONObject(scene.toString())
        val index = objects().length()
        changed.getJSONArray("objects").put(newObject(kind, "$kind ${index + 1}").apply {
            put("x", ((index % 3) - 1) * 0.65); put("scale", 0.65)
        })
        selected = index
        replace(changed, true)
    }
    private fun duplicate() {
        val source = selectedObject() ?: return
        if (objects().length() >= 32) { toast("Scene limit: 32 objects"); return }
        val changed = JSONObject(scene.toString())
        val copy = JSONObject(source.toString()).apply { put("name", optString("name") + " copy"); put("x", getDouble("x") + 0.3) }
        changed.getJSONArray("objects").put(copy); selected = objects().length()
        replace(changed, true)
    }
    private fun deleteObject() {
        if (selectedObject() == null) return
        val changed = JSONObject(scene.toString()); changed.getJSONArray("objects").remove(selected)
        replace(changed, true)
    }
    private fun record() { undo.addLast(scene.toString()); while (undo.size > 30) undo.removeFirst(); redo.clear() }
    private fun replace(value: JSONObject, remember: Boolean, refresh: Boolean = true): Boolean {
        val result = NativeBridge.nativeSetScene(value.toString())
        if (result != "ok") { toast(result); log(result); return false }
        if (remember) record()
        scene = value; selected = selected.coerceIn(0, max(0, objects().length() - 1)); dirty = true
        titleView?.text = "Scene •"
        if (refresh) { refreshHierarchy(); refreshInspector() }
        return true
    }
    private fun historyBack() {
        if (undo.isEmpty()) { toast("Nothing to undo"); return }
        val previous = undo.removeLast(); redo.addLast(scene.toString()); replace(JSONObject(previous), false)
    }
    private fun historyForward() {
        if (redo.isEmpty()) { toast("Nothing to redo"); return }
        val next = redo.removeLast(); undo.addLast(scene.toString()); replace(JSONObject(next), false)
    }
    private fun togglePlay() {
        playing = !playing
        log(NativeBridge.nativeExecConsole(if (playing) "play" else "stop"))
        playButton?.text = if (playing) "■ Stop" else "▶ Play"
    }
    private fun cameraZoom(value: Float) {
        zoom = value.coerceIn(1.5f, 20f)
        log(NativeBridge.nativeExecConsole("camera $zoom"))
    }
    private fun readScene(file: AtomicFile): JSONObject {
        val text = file.openRead().use { input ->
            val bytes = ByteArray(65537)
            var count = 0
            while (count < bytes.size) { val n = input.read(bytes, count, bytes.size - count); if (n < 0) break; count += n }
            require(count <= 65536) { "Scene is too large" }
            String(bytes, 0, count, Charsets.UTF_8)
        }
        return JSONObject(text)
    }
    private fun writeScene(file: AtomicFile) {
        file.baseFile.parentFile?.mkdirs()
        val stream = file.startWrite()
        try { stream.write(scene.toString(2).toByteArray(Charsets.UTF_8)); file.finishWrite(stream) }
        catch (e: Exception) { file.failWrite(stream); throw e }
    }
    private fun save() {
        try { writeScene(sceneFile); writeScene(recoveryFile); dirty = false; titleView?.text = "Scene"; log("Scene saved to app storage"); toast("Scene saved") }
        catch (e: Exception) { log("Save failed: ${e.message}"); toast("Could not save scene") }
    }
    fun saveRecovery() {
        try { writeScene(recoveryFile) } catch (e: Exception) { log("Recovery save failed: ${e.message}") }
    }
    private fun confirmLoad() {
        if (!sceneFile.baseFile.exists()) { toast("No saved scene yet. Use Save first."); return }
        AlertDialog.Builder(context).setTitle("Load saved scene?").setMessage("Current edits remain available in Undo.")
            .setPositiveButton("Load") { _, _ ->
                try { if (replace(readScene(sceneFile), true)) log("Saved scene loaded") }
                catch (e: Exception) { toast("Could not load scene"); log(e.message ?: "Load failed") }
            }.setNegativeButton("Cancel", null).show()
    }
    private fun log(message: String) {
        logs.addLast(message); while (logs.size > 40) logs.removeFirst()
        logView?.text = logs.joinToString("\n")
    }
    private fun toast(message: String) { Toast.makeText(context, message, Toast.LENGTH_SHORT).show() }
    private fun installDragControls() {
        var lastX = 0f; var lastY = 0f; var dragging = false
        viewport.touchSink = { event ->
            if (playing || selectedObject() == null) false
            else {
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> { lastX = event.x; lastY = event.y; dragging = true; record() }
                    MotionEvent.ACTION_MOVE -> if (dragging && event.pointerCount == 1) {
                        val changed = JSONObject(scene.toString())
                        val target = changed.getJSONArray("objects").getJSONObject(selected)
                        val units = 1.1547f * zoom / max(1, viewport.height)
                        target.put("x", (target.getDouble("x") + (event.x - lastX) * units).coerceIn(-1000.0, 1000.0))
                        target.put("y", (target.getDouble("y") - (event.y - lastY) * units).coerceIn(-1000.0, 1000.0))
                        replace(changed, false, false); lastX = event.x; lastY = event.y
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { dragging = false; refreshInspector() }
                }
                true
            }
        }
    }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); attached = true; post(statsTick) }
    override fun onDetachedFromWindow() { attached = false; removeCallbacks(statsTick); super.onDetachedFromWindow() }

    companion object {
        private fun newObject(kind: String, name: String) = JSONObject().apply {
            put("kind", kind); put("name", name)
            for (key in listOf("x", "y", "z", "rx", "ry", "rz")) put(key, 0.0)
            put("scale", 1.0); put("r", 1.0); put("g", 1.0); put("b", 1.0)
        }
        private fun defaultScene() = JSONObject().apply {
            put("version", 1)
            put("objects", JSONArray().put(newObject("Cube", "Cube 1").apply {
                put("rx", 20.0); put("ry", 32.0); put("scale", 0.9)
            }))
        }
    }
}
