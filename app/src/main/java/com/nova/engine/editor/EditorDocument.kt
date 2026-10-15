package com.nova.engine.editor

import com.nova.engine.NativeBridge
import org.json.JSONObject
import java.util.ArrayDeque

/** Owns authoring state. Native validation succeeds before any undo/live state changes. */
class EditorDocument(initial: JSONObject) {
    private data class Snapshot(val json: String, val selected: Int)
    var json = initial; private set
    var selected = 0
    var dirty = false; private set
    private val undo = ArrayDeque<Snapshot>()
    private val redo = ArrayDeque<Snapshot>()
    val objects get() = json.getJSONArray("objects")
    fun selectedObject(): JSONObject? = objects.optJSONObject(selected)
    fun canUndo() = !undo.isEmpty()
    fun canRedo() = !redo.isEmpty()
    fun record() { undo.addLast(Snapshot(json.toString(), selected)); trimHistory(30); redo.clear() }
    fun replace(next: JSONObject, remember: Boolean = true): String {
        val result = NativeBridge.nativeSetScene(next.toString())
        if (result != "ok") return result
        if (remember) record()
        json = next; selected = selected.coerceIn(0, maxOf(0, objects.length()-1)); dirty = true
        return "ok"
    }
    fun edit(change: (JSONObject) -> Unit): String {
        val next = JSONObject(json.toString()); change(next); return replace(next)
    }
    fun undo(): Boolean {
        if (undo.isEmpty()) return false
        val target = undo.peekLast()
        if (NativeBridge.nativeSetScene(target.json) != "ok") return false
        redo.addLast(Snapshot(json.toString(),selected)); undo.removeLast()
        json=JSONObject(target.json); selected=target.selected.coerceIn(0,maxOf(0,objects.length()-1)); dirty=true; return true
    }
    fun redo(): Boolean {
        if (redo.isEmpty()) return false
        val target=redo.peekLast()
        if(NativeBridge.nativeSetScene(target.json)!="ok") return false
        undo.addLast(Snapshot(json.toString(),selected)); redo.removeLast()
        json=JSONObject(target.json); selected=target.selected.coerceIn(0,maxOf(0,objects.length()-1)); dirty=true; return true
    }
    fun saved() { dirty=false }
    fun trimHistory(count: Int) { while(undo.size>count) undo.removeFirst(); while(redo.size>count) redo.removeFirst() }
}
