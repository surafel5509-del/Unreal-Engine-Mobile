package com.nova.engine.editor

import android.content.Context
import android.util.AtomicFile
import com.nova.engine.NativeBridge
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

/** App-private, bounded JSON projects. Never resolves an imported/user-supplied path. */
class ProjectStore(context: Context) {
    private val root = File(context.filesDir,"studio/projects").apply { mkdirs() }
    private val prefs=context.getSharedPreferences("studio",Context.MODE_PRIVATE)
    var projectId: String = prefs.getString("project",null)?.takeIf { validId(it) && File(root,it).isDirectory } ?: ""
        private set
    var sceneName: String = prefs.getString("scene","Main")?.takeIf { validName(it) } ?: "Main"
        private set
    val projectDir get() = File(root,projectId)
    var warning: String? = null; private set
    val projectName get() = runCatching { read(File(projectDir,"project.json")).optString("name","Project") }.getOrDefault("Project")
    init {
        if(projectId.isEmpty()) {
            var initial=SceneTemplates.island()
            val legacy=listOf(File(context.filesDir,"editor/recovery.json"),File(context.filesDir,"editor/scene.json"))
                .filter { it.exists() || File(it.path+".bak").exists() }.sortedByDescending { it.lastModified() }
            for(old in legacy) {
                val value=runCatching { read(old).also { validate(it) } }.getOrNull()
                if(value!=null) { initial=value;break }
                warning="A legacy scene could not be read. Original files have been preserved."
            }
            create("Emerald Valley",initial)
        }
    }
    private fun validId(id:String)=id.matches(Regex("[a-f0-9-]{36}"))
    fun validName(name:String)=name.matches(Regex("[A-Za-z0-9 _-]{1,40}"))
    private fun leaf(folder:String,name:String):File { require(validName(name)) { "Use 1–40 letters, numbers, spaces, - or _" }; return File(projectDir,"$folder/$name.json") }
    private fun select(id:String,scene:String) { require(validId(id)&&validName(scene)); projectId=id; sceneName=scene; prefs.edit().putString("project",id).putString("scene",scene).apply() }
    private fun write(file:File,value:JSONObject) {
        val bytes=value.toString().toByteArray(Charsets.UTF_8); require(bytes.size<=65536) { "Document exceeds 64 KiB" }
        file.parentFile?.mkdirs(); val atomic=AtomicFile(file); val out=atomic.startWrite()
        try { out.write(bytes); atomic.finishWrite(out) } catch(e:Exception) { atomic.failWrite(out); throw e }
    }
    private fun read(file:File):JSONObject {
        val bytes=AtomicFile(file).openRead().use { input ->
            val buffer=ByteArray(65537); var n=0
            while(n<buffer.size) { val k=input.read(buffer,n,buffer.size-n); if(k<0) break; n+=k }
            require(n<=65536) { "Document exceeds 64 KiB" }; buffer.copyOf(n)
        }
        return JSONObject(String(bytes,Charsets.UTF_8))
    }
    private fun validate(scene:JSONObject) { val result=NativeBridge.nativeValidateScene(scene.toString()); require(result=="ok") { result } }
    fun create(name:String,initial:JSONObject=SceneTemplates.blank()) {
        require(validName(name)); require(projects().size<16) { "Project limit: 16" }; validate(initial)
        val id=UUID.randomUUID().toString(); val directory=File(root,id)
        try {
            write(File(directory,"project.json"),JSONObject().put("name",name).put("version",1))
            write(File(directory,"scenes/Main.json"),initial)
            write(File(directory,"scripts/Preview.json"),JSONObject().put("source","graphics\nscene\nplay\nstats"))
        } catch(e:Exception) { directory.deleteRecursively(); throw e }
        select(id,"Main")
    }
    fun projects(): List<Pair<String,String>> = root.listFiles()?.filter { it.isDirectory && validId(it.name) }
        ?.mapNotNull { runCatching { it.name to read(File(it,"project.json")).getString("name") }.getOrNull() }?.sortedBy { it.second } ?: emptyList()
    fun openProject(id:String):JSONObject { require(validId(id)); val doc=read(File(root,"$id/scenes/Main.json")); validate(doc); select(id,"Main"); return doc }
    fun scenes()=names("scenes")
    fun prefabs()=names("prefabs")
    fun scripts()=names("scripts")
    private fun names(folder:String)=File(projectDir,folder).listFiles()?.filter { it.extension=="json" && validName(it.nameWithoutExtension) }?.map { it.nameWithoutExtension }?.sorted() ?: emptyList()
    fun load(recovery:Boolean=false):JSONObject {
        val saved=leaf("scenes",sceneName); val recover=leaf("recovery",sceneName)
        if(recovery && (recover.exists() || File(recover.path+".bak").exists()) && recover.lastModified()>=saved.lastModified()) {
            try { return read(recover).also { validate(it) } }
            catch(e:Exception) { warning="Recovery could not be read; trying the explicit saved scene. Original files preserved." }
        }
        return read(saved).also { validate(it) }
    }

    fun save(value:JSONObject,recovery:Boolean=false) { validate(value); write(leaf(if(recovery) "recovery" else "scenes",sceneName),value) }
    fun newScene(name:String,value:JSONObject) { require(!scenes().contains(name)) { "Scene already exists" }; require(scenes().size<16); validate(value); write(leaf("scenes",name),value); select(projectId,name) }
    fun openScene(name:String):JSONObject { val value=read(leaf("scenes",name)); validate(value); select(projectId,name); return value }
    fun savePrefab(name:String,obj:JSONObject) {
        require(prefabs().size<32 || prefabs().contains(name)); validate(JSONObject().put("version",1).put("objects",JSONArray().put(obj)))
        write(leaf("prefabs",name),JSONObject().put("object",obj))
    }
    fun prefab(name:String):JSONObject=read(leaf("prefabs",name)).getJSONObject("object")
    fun script(name:String):String=read(leaf("scripts",name)).getString("source")
    fun saveScript(name:String,source:String) { require(source.length<=8192); require(scripts().size<16 || scripts().contains(name)); write(leaf("scripts",name),JSONObject().put("source",source)) }
    fun exportProject():String {
        val result=JSONObject().put("format","nova-project").put("version",1).put("name",projectName)
        for(folder in listOf("scenes","prefabs","scripts")) {
            val entries=JSONObject(); for(name in names(folder)) entries.put(name,read(leaf(folder,name))); result.put(folder,entries)
        }
        val text=result.toString(); require(text.toByteArray().size<=524288) { "Export exceeds 512 KiB budget" }; return text
    }
    fun importProject(text:String):JSONObject {
        require(text.toByteArray().size<=524288) { "Import exceeds 512 KiB" }
        val input=JSONObject(text)
        if(input.optString("format")!="nova-project") { validate(input); create("Imported scene",input); return load() }
        require(input.get("version") is Number && input.getDouble("version")==1.0); require(projects().size<16)
        val scenes=input.getJSONObject("scenes"); require(scenes.has("Main") && scenes.length() in 1..16)
        val prefabs=input.optJSONObject("prefabs")?:JSONObject(); require(prefabs.length()<=32)
        val scripts=input.optJSONObject("scripts")?:JSONObject(); require(scripts.length()<=16)
        for(name in scenes.keys()) { require(validName(name)); validate(scenes.getJSONObject(name)) }
        for(name in prefabs.keys()) { require(validName(name)); validate(JSONObject().put("version",1).put("objects",JSONArray().put(prefabs.getJSONObject(name).getJSONObject("object")))) }
        for(name in scripts.keys()) { require(validName(name)); require(scripts.getJSONObject(name).getString("source").length<=8192) }
        val id=UUID.randomUUID().toString(); val staging=File(root,".import-$id")
        val projectName=input.optString("name","Imported project").take(40).let { if(validName(it)) it else "Imported project" }
        try {
            write(File(staging,"project.json"),JSONObject().put("name",projectName).put("version",1))
            for((folder,entries) in listOf("scenes" to scenes,"prefabs" to prefabs,"scripts" to scripts))
                for(name in entries.keys()) write(File(staging,"$folder/$name.json"),entries.getJSONObject(name))
            require(staging.renameTo(File(root,id))) { "Could not finish import" }
        } catch(e:Exception) { staging.deleteRecursively(); throw e }
        select(id,"Main"); return load()
    }
}
