package com.nova.engine.editor

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.ViewGroup
import android.widget.*
import com.nova.engine.NativeBridge
import com.nova.engine.NovaSurfaceView
import com.nova.engine.runtime.NovaRuntime
import org.json.JSONArray
import org.json.JSONObject
import java.util.ArrayDeque
import kotlin.math.*

/** Workspace controller. Document history, persistence, chrome and layout live in separate modules. */
class EditorView(context:Context,private val viewport:NovaSurfaceView,
    private val exportRequested:(String)->Unit={},private val importRequested:()->Unit={}) : LinearLayout(context) {
    private val ui=EditorUi(context)
    private val store=ProjectStore(context)
    private var recoveryBlocked=false
    private var startupWarning:String?=null
    private var doc=EditorDocument(runCatching {store.load(true)}.getOrElse {
        recoveryBlocked=true
        startupWarning="Your scene could not be read. Original files have not been changed. A temporary scene is open; Save creates a separate recovery scene. You can also import a backup."
        SceneTemplates.blank()
    })
    private var playing=false; private var game=false; private var mode="Move";private var axis="Free"
    private var dockTab="Assets";private var category="Meshes";private var query=""
    private lateinit var workspace:SplitWorkspace
    private lateinit var hierarchy:LinearLayout;private lateinit var inspector:LinearLayout;private lateinit var dock:LinearLayout
    private var logText:TextView?=null;private var outputText:TextView?=null;private var titleText:TextView?=null
    private var statusText:TextView?=null;private var hud:TextView?=null;private var playLabel:TextView?=null
    private val actions=mutableMapOf<String,View>()
    private val logs=ArrayDeque<String>();private var output="Build output will appear here."
    private var attached=false;private var lastStats="";private var ticks=0
    private val prefs=context.getSharedPreferences("studio-settings",Context.MODE_PRIVATE)
    private val ticker=object:Runnable {override fun run(){if(!attached)return
        lastStats=NativeBridge.nativeExecConsole("stats")
        val fps=Regex("fps=([0-9.]+)").find(lastStats)?.groupValues?.get(1)?.toDoubleOrNull()?.roundToInt()?:0
        val cpu=Regex("cpu_ms=([0-9.]+)").find(lastStats)?.groupValues?.get(1)?.toDoubleOrNull()?:0.0
        statusText?.text="  ● ${if(playing) "RUNNING" else "READY"}   •   ${doc.objects.length()}/32 objects   •   $fps FPS   •   Native ${"%.1f".format(cpu)} ms    |    Android · GLES"
        hud?.text="${if(game) "GAME PREVIEW" else "SCENE"}   /   $fps FPS"
        if(dockTab=="Profiler") refreshDock()
        ticks++;if(ticks%30==0 && doc.dirty) saveRecovery()
        postDelayed(this,1000)
    }}
    init {
        orientation=VERTICAL;setBackgroundColor(StudioColors.background)
        NativeBridge.nativeSetScene(doc.json.toString()); applyCamera()
        NovaRuntime.targetFps=prefs.getInt("fps",60)
        log("Nova Studio 0.3 • ${NativeBridge.nativeExecConsole("graphics")}")
        log("Opened ${store.projectName} / ${store.sceneName}")
        installGestures();rebuild()
        (startupWarning?:store.warning)?.let { message -> log(message);post { AlertDialog.Builder(context).setTitle("Scene recovery").setMessage(message).setPositiveButton("OK",null).show() } }
    }
    private fun lp(w:Int,h:Int,weight:Float=0f)=LayoutParams(w,h,weight)
    private fun button(label:String,key:String=label,icon:String?=null,action:()->Unit):View = ui.button(label,key,icon,action).also {actions[key]=it}
    private fun iconButton(label:String,key:String,icon:String,action:()->Unit)=ui.button("",key,icon,action).apply {contentDescription=label;minimumWidth=ui.dp(40)}.also {actions[key]=it}
    private fun safe(block:()->Unit){try{block()}catch(e:Exception){log("Error: ${e.message}");toast(e.message?:"Operation failed")}}
    private fun toast(text:String)=Toast.makeText(context,text,Toast.LENGTH_SHORT).show()
    private fun log(text:String){logs.addLast(text);while(logs.size>60)logs.removeFirst();logText?.text=logs.joinToString("\n")}
    private fun change(next:JSONObject,remember:Boolean=true,refresh:Boolean=true):Boolean {
        val result=doc.replace(next,remember)
        if(result!="ok"){log(result);toast(result);return false}
        if(refresh)refreshAll();return true
    }
    private fun edit(block:(JSONObject)->Unit){safe{val next=JSONObject(doc.json.toString());block(next);change(next)}}
    private fun editSelected(block:(JSONObject)->Unit){if(doc.selectedObject()!=null)edit {block(it.getJSONArray("objects").getJSONObject(doc.selected))}}

    fun rebuild(){
        (viewport.parent as? ViewGroup)?.removeView(viewport);removeAllViews();actions.clear();logText=null;outputText=null
        val top=ui.row().apply {setBackgroundColor(StudioColors.panel)}
        top.addView(StudioIcon(context,"cube",StudioColors.blue),lp(ui.dp(42),ui.dp(42)))
        val brand=ui.column();brand.addView(ui.text("NOVA STUDIO",13f).apply{typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL)})
        titleText=ui.text(store.projectName,9f,StudioColors.muted);brand.addView(titleText);brand.setOnClickListener{projects()};brand.tag="projects"
        top.addView(brand,lp(ui.dp(130),-1))
        val tabs=ui.row()
        for((label,icon) in listOf("Scene" to "scene","Game" to "game","Build" to "build","Settings" to "settings")) {
            val v=ui.column().apply{tag=label.lowercase();contentDescription=label;gravity=Gravity.CENTER;isClickable=true
                addView(StudioIcon(context,icon,if((label=="Game")==game && label in listOf("Scene","Game"))StudioColors.blue else StudioColors.muted),lp(ui.dp(22),ui.dp(22)))
                addView(ui.text(label,10f).apply{gravity=Gravity.CENTER});setOnClickListener{when(label){"Scene"->setGame(false);"Game"->setGame(true);"Build"->buildMenu();else->settings()}}
            };actions[label.lowercase()]=v;tabs.addView(v,lp(ui.dp(54),ui.dp(48)))
        }
        val play=ui.column().apply{tag="play";contentDescription="Play or stop preview";gravity=Gravity.CENTER;isClickable=true
            addView(StudioIcon(context,if(playing)"stop" else "play",StudioColors.blue),lp(ui.dp(22),ui.dp(22)))
            playLabel=ui.text(if(playing)"Stop" else "Play",10f).apply{gravity=Gravity.CENTER};addView(playLabel);setOnClickListener{togglePlay()}}
        actions["play"]=play;tabs.addView(play,lp(ui.dp(54),ui.dp(48)))
        tabs.addView(iconButton("Save scene","save","save"){save()});tabs.addView(iconButton("Undo","undo","undo"){if(doc.undo()){refreshAll();applyCamera()}else toast("Nothing to undo")})
        tabs.addView(iconButton("Redo","redo","redo"){if(doc.redo()){refreshAll();applyCamera()}else toast("Nothing to redo")})
        tabs.addView(iconButton("Import JSON project","import","import"){importRequested()})
        top.addView(ui.horizontal(tabs),lp(0,ui.dp(52),1f));addView(top,lp(-1,ui.dp(52)))
        val toolRow=ui.row().apply{setBackgroundColor(StudioColors.raised)}
        toolRow.addView(button("Add","add","plus"){addMenu()})
        for(tool in listOf("Move","Rotate","Scale","Orbit")) toolRow.addView(button(tool,tool.lowercase(),tool.lowercase()){mode=tool;refreshTools()})
        for(a in listOf("Free","X","Y","Z"))toolRow.addView(button(a,"axis_$a"){axis=a;refreshTools()})
        toolRow.addView(button("Frame","frame"){frameSelected()});toolRow.addView(button("Reset view","resetView"){setCamera(SceneTemplates.camera());applyCamera()})
        addView(ui.horizontal(toolRow),lp(-1,ui.dp(42)))

        val left=ui.column();val heading=ui.row();heading.addView(ui.text("Hierarchy",12f),lp(0,ui.dp(32),1f));heading.addView(iconButton("Add object","hierarchyAdd","plus"){addMenu()});left.addView(heading)
        val search=EditText(context).apply{hint="Search objects";setHintTextColor(StudioColors.muted);setTextColor(StudioColors.text);textSize=11f;setSingleLine();setText(query);setPadding(ui.dp(8),0,ui.dp(8),0);background=ui.shape(StudioColors.background)}
        search.addTextChangedListener(object:TextWatcher{override fun beforeTextChanged(s:CharSequence?,a:Int,b:Int,c:Int){};override fun onTextChanged(s:CharSequence?,a:Int,b:Int,c:Int){query=s.toString();refreshHierarchy()};override fun afterTextChanged(e:Editable?){} })
        left.addView(search,lp(-1,ui.dp(32)));hierarchy=ui.column();left.addView(ui.scroll(hierarchy),lp(-1,0,1f))
        val center=ui.column();val vh=ui.row();hud=ui.text("SCENE  /  PERSPECTIVE",10f,StudioColors.muted);vh.addView(hud,lp(0,ui.dp(28),1f))
        vh.addView(ui.text("Y ↑   X →",10f,StudioColors.blue));center.addView(vh,lp(-1,ui.dp(28)))
        val frame=FrameLayout(context);frame.addView(viewport,FrameLayout.LayoutParams(-1,-1))
        val pad=NavigationPad(context){x,y->pan(x*.08f,y*.08f)}.apply{tag="joystick";visibility=if(game)VISIBLE else GONE}
        frame.addView(pad,FrameLayout.LayoutParams(ui.dp(80),ui.dp(80),Gravity.BOTTOM or Gravity.START).apply{setMargins(ui.dp(8),0,0,ui.dp(8))});actions["joystick"]=pad
        center.addView(frame,lp(-1,0,1f))
        val right=ui.column();ui.title(right,"Inspector");inspector=ui.column();right.addView(ui.scroll(inspector),lp(-1,0,1f))
        val bottom=ui.column();val dockTabs=ui.row()
        for(t in listOf("Assets","Console","Output","Profiler"))dockTabs.addView(button(t,"tab_$t",if(t=="Assets")"folder" else if(t=="Console")"console" else "output"){dockTab=t;refreshDock()})
        bottom.addView(ui.horizontal(dockTabs),lp(-1,ui.dp(34)));dock=ui.column();bottom.addView(dock,lp(-1,0,1f))
        workspace=SplitWorkspace(context,left,center,right,bottom);workspace.game=game;addView(workspace,lp(-1,0,1f))
        statusText=ui.text("Preparing native viewport…",9f,StudioColors.muted);addView(statusText,lp(-1,ui.dp(22)))
        refreshAll()
    }
    private fun refreshTools(){
        for(t in listOf("Move","Rotate","Scale","Orbit"))actions[t.lowercase()]?.background=ui.shape(if(t==mode)StudioColors.selected else StudioColors.raised)
        for(a in listOf("Free","X","Y","Z"))actions["axis_$a"]?.background=ui.shape(if(a==axis)StudioColors.selected else StudioColors.raised)
        actions["undo"]?.alpha=if(doc.canUndo())1f else .35f;actions["redo"]?.alpha=if(doc.canRedo())1f else .35f
    }
    private fun refreshAll(){titleText?.text="${store.projectName} / ${store.sceneName}${if(doc.dirty)" •" else ""}";refreshHierarchy();refreshInspector();refreshDock();refreshTools()}
    private fun refreshHierarchy(){if(!::hierarchy.isInitialized)return;hierarchy.removeAllViews()
        hierarchy.addView(ui.text("▾ ${store.sceneName}",11f,StudioColors.muted));hierarchy.addView(ui.text("   Main Camera",11f,StudioColors.muted).apply{setOnClickListener{setCamera(SceneTemplates.camera());applyCamera()}})
        for(i in 0 until doc.objects.length()){
            val obj=doc.objects.getJSONObject(i);val name=obj.optString("name",obj.getString("kind"));if(!name.contains(query,true))continue
            val row=ui.row().apply{tag="object_$i";contentDescription="Select $name";setBackgroundColor(if(i==doc.selected)StudioColors.selected else StudioColors.panel);setOnClickListener{doc.selected=i;refreshHierarchy();refreshInspector()}}
            row.addView(StudioIcon(context,obj.getString("kind"),if(i==doc.selected)StudioColors.blue else StudioColors.muted),lp(ui.dp(23),ui.dp(23)))
            row.addView(ui.text(name,11f).apply{maxLines=1;ellipsize=android.text.TextUtils.TruncateAt.END},lp(0,ui.dp(36),1f))
            row.addView(StudioIcon(context,"eye",if(obj.optBoolean("enabled",true))StudioColors.muted else StudioColors.border).apply{contentDescription="Toggle $name visibility";setOnClickListener{edit {val o=it.getJSONArray("objects").getJSONObject(i);o.put("enabled",!o.optBoolean("enabled",true))}}},lp(ui.dp(32),ui.dp(36)))
            hierarchy.addView(row,lp(-1,ui.dp(36)))
        }
    }
    private fun refreshInspector(){if(!::inspector.isInitialized)return;inspector.removeAllViews();val obj=doc.selectedObject()
        if(obj==null){inspector.addView(ui.text("Select an object in the hierarchy or tap it in the viewport.",12f,StudioColors.muted));return}
        val top=ui.row();top.addView(StudioIcon(context,obj.getString("kind"),StudioColors.blue),lp(ui.dp(30),ui.dp(30)))
        val name=EditText(context).apply{setText(obj.optString("name",obj.getString("kind")));setTextColor(StudioColors.text);textSize=12f;setSingleLine();tag="objectName"}
        top.addView(name,lp(0,ui.dp(40),1f));inspector.addView(top)
        ui.title(inspector,"Transform")
        val fields=linkedMapOf<String,EditText>()
        for((label,keys) in listOf("Position" to listOf("x","y","z"),"Rotation" to listOf("rx","ry","rz"),"Scale" to listOf("sx","sy","sz"))){
            inspector.addView(ui.text(label,10f,StudioColors.muted));val row=ui.row()
            for((index,key) in keys.withIndex()){
                val cell=ui.column();val header=ui.text(listOf("X","Y","Z")[index],9f,listOf(0xffe58a95.toInt(),0xff87cbb0.toInt(),0xff80b8f4.toInt())[index]);header.gravity=Gravity.CENTER;cell.addView(header)
                val value=obj.optDouble(key,if(key.startsWith("s"))obj.optDouble("scale",1.0) else 0.0)
                val input=EditText(context).apply{setText(String.format(java.util.Locale.US,"%.2f",value));textSize=11f;setTextColor(StudioColors.text);inputType=InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL or InputType.TYPE_NUMBER_FLAG_SIGNED
                    setSingleLine();setSelectAllOnFocus(true);setPadding(ui.dp(4),0,ui.dp(4),0);background=ui.shape(StudioColors.background);tag="field_$key";contentDescription="$label $key"}
                fields[key]=input;cell.addView(input,lp(-1,ui.dp(34)));row.addView(cell,lp(0,-2,1f))
            };inspector.addView(row)
        }
        val buttons=ui.row();buttons.addView(button("Apply","apply"){safe{
            val values=fields.mapValues{(key,input)->input.text.toString().toDoubleOrNull()?.takeIf{it.isFinite()}?:error("Invalid $key")}
            editSelected{target->for((key,value)in values)target.put(key,value);target.put("name",name.text.toString().take(64))}
        }})
        buttons.addView(iconButton("Duplicate object","duplicate","duplicate"){duplicate()});buttons.addView(iconButton("Delete object","delete","delete"){edit{it.getJSONArray("objects").remove(doc.selected)}});inspector.addView(buttons)
        ui.title(inspector,"Mesh Renderer  ·  ${obj.getString("kind")}")
        val colors=ui.row();for((label,rgb)in listOf("White" to 0xffffff,"Blue" to 0x719fff,"Mint" to 0x78dab0,"Gold" to 0xeac46a,"Rose" to 0xef8a9d)){
            val swatch=View(context).apply{tag="color_$label";contentDescription="$label material tint";background=ui.shape(rgb or 0xff000000.toInt());setOnClickListener{editSelected{it.put("r",((rgb shr 16)and 255)/255.0);it.put("g",((rgb shr 8)and 255)/255.0);it.put("b",(rgb and 255)/255.0)}}}
            colors.addView(swatch,lp(0,ui.dp(32),1f).apply{setMargins(ui.dp(3),ui.dp(4),ui.dp(3),ui.dp(4))})
        };inspector.addView(colors)
        inspector.addView(CheckBox(context).apply{text="Rotate during Play";textSize=11f;setTextColor(StudioColors.text);isChecked=obj.optBoolean("spin",false);tag="spin";setOnCheckedChangeListener{_,checked->editSelected{it.put("spin",checked)}}})
        inspector.addView(button("Save as prefab","prefab","prefab"){saveSelectedPrefab()})
        if(obj.getString("kind")=="Terrain"){
            ui.title(inspector,"Terrain  ·  17 × 17 heightfield")
            inspector.addView(ui.text("Center brush • radius 3 samples",10f,StudioColors.muted))
            val brush=ui.row();brush.addView(button("Raise","terrainRaise"){sculpt(.15)});brush.addView(button("Lower","terrainLower"){sculpt(-.15)});inspector.addView(brush)
            inspector.addView(button("Smooth","terrainSmooth"){smoothTerrain()})
            inspector.addView(button("Reset heights","terrainReset"){edit{it.put("terrain",SceneTemplates.heights())}})
        }
    }
    private fun refreshDock(){if(!::dock.isInitialized)return;dock.removeAllViews();logText=null;outputText=null
        for(t in listOf("Assets","Console","Output","Profiler"))actions["tab_$t"]?.background=ui.shape(if(t==dockTab)StudioColors.selected else StudioColors.raised)
        when(dockTab){
            "Assets"->assetPanel()
            "Console"->{val lines=ui.text(logs.joinToString("\n"),10f,StudioColors.muted).apply{typeface=Typeface.MONOSPACE;setTextIsSelectable(true)};logText=lines;dock.addView(ui.scroll(lines),lp(-1,0,1f));val controls=ui.row()
                val input=EditText(context).apply{hint="help / stats / scene / orbit";setHintTextColor(StudioColors.muted);setTextColor(StudioColors.text);textSize=11f;setSingleLine();tag="consoleInput"};controls.addView(input,lp(0,ui.dp(36),1f));controls.addView(button("Run","consoleRun","console"){runCommands(input.text.toString());input.setText("")});dock.addView(controls)}
            "Output"->{outputText=ui.text(output,10f,StudioColors.muted).apply{typeface=Typeface.MONOSPACE;setTextIsSelectable(true)};dock.addView(ui.scroll(outputText!!),lp(-1,-1))}
            else->{dock.addView(ui.scroll(ui.text(lastStats,10f,StudioColors.muted).apply{typeface=Typeface.MONOSPACE;setTextIsSelectable(true)}),lp(-1,-1))}
        }
    }
    private fun assetPanel(){
        val row=ui.row();val folders=ui.column();for(folder in listOf("Meshes","Scenes","Prefabs","Scripts")) folders.addView(ui.text(folder,10f,if(folder==category)StudioColors.blue else StudioColors.muted).apply{tag="assets_$folder";minimumHeight=ui.dp(32);setOnClickListener{category=folder;refreshDock()}})
        row.addView(ui.scroll(folders),lp(ui.dp(82),-1))
        val content=ui.column();val crumbs=ui.row();crumbs.addView(ui.text("Assets  ›  $category",10f,StudioColors.muted),lp(0,ui.dp(26),1f));crumbs.addView(ui.text("+",16f,StudioColors.blue).apply{contentDescription="Create $category asset";setOnClickListener{when(category){"Scenes"->newScene();"Scripts"->askName("Script name","Preview"){editScript(it)};"Prefabs"->saveSelectedPrefab();else->addMenu()}}});content.addView(crumbs)
        val cards=ui.row();val entries=when(category){"Meshes"->SceneTemplates.meshes;"Scenes"->store.scenes();"Prefabs"->store.prefabs();else->store.scripts()}
        if(entries.isEmpty())cards.addView(ui.text("No $category yet. Use + or the Inspector to create one.",11f,StudioColors.muted))
        for(name in entries){val icon=when(category){"Meshes"->name;"Scenes"->"scene";"Prefabs"->"prefab";else->"script"};val card=ui.column().apply{
                tag=if(category=="Meshes")"add$name" else "asset_${category}_$name";contentDescription="$category: $name";gravity=Gravity.CENTER;background=ui.shape(StudioColors.raised)
                addView(StudioIcon(context,icon,StudioColors.blue),lp(ui.dp(26),ui.dp(26)));addView(ui.text(name,9f).apply{gravity=Gravity.CENTER;maxLines=1;ellipsize=android.text.TextUtils.TruncateAt.END})
                setOnClickListener{safe{when(category){"Meshes"->addObject(name);"Scenes"->{if(save())open(store.openScene(name))};"Prefabs"->addPrefab(store.prefab(name));else->editScript(name)}}}
            };cards.addView(card,lp(ui.dp(74),ui.dp(52)).apply{setMargins(ui.dp(4),0,ui.dp(4),0)})}
        content.addView(ui.horizontal(cards),lp(-1,0,1f));row.addView(content,lp(0,-1,1f));dock.addView(row,lp(-1,-1))
    }
    private fun saveSelectedPrefab() {
        val obj=doc.selectedObject() ?: return toast("Select an object before creating a prefab")
        askName("Prefab name",obj.optString("name","Prefab")) { name ->
            store.savePrefab(name,JSONObject(obj.toString()))
            log("Prefab saved: $name");category="Prefabs";dockTab="Assets";refreshDock()
        }
    }
    private fun addMenu(){AlertDialog.Builder(context).setTitle("Add native mesh").setItems(SceneTemplates.meshes.toTypedArray()){_,i->addObject(SceneTemplates.meshes[i])}.show()}
    private fun addObject(kind:String){addPrefab(SceneTemplates.objectOf(kind,"$kind ${doc.objects.length()+1}"))}
    private fun addPrefab(obj:JSONObject){if(doc.objects.length()>=32){toast("32-object mobile budget reached");return};val next=JSONObject(doc.json.toString());next.getJSONArray("objects").put(JSONObject(obj.toString()));if(change(next)){doc.selected=doc.objects.length()-1;refreshAll()}}
    private fun duplicate(){doc.selectedObject()?.let{addPrefab(JSONObject(it.toString()).apply{put("name",optString("name")+" copy");put("x",optDouble("x")+.3)})}}
    private fun togglePlay(){playing=!playing;log(NativeBridge.nativeExecConsole(if(playing)"play" else "stop"));playLabel?.text=if(playing)"Stop" else "Play"
        (actions["play"] as? LinearLayout)?.let{it.removeViewAt(0);it.addView(StudioIcon(context,if(playing)"stop" else "play",StudioColors.blue),0,lp(ui.dp(22),ui.dp(22)))};refreshTools()}
    private fun setGame(value:Boolean){game=value;workspace.game=value;actions["joystick"]?.visibility=if(value)VISIBLE else GONE;NativeBridge.nativeExecConsole("view ${if(value)"game" else "scene"}")
        for(key in listOf("scene","game")){val active=(key=="game")==value;(actions[key] as? LinearLayout)?.let{val icon=it.getChildAt(0) as StudioIcon;icon.tint=if(active)StudioColors.blue else StudioColors.muted;icon.invalidate();it.setBackgroundColor(if(active)StudioColors.raised else StudioColors.panel)}}}
    private fun camera():JSONObject=doc.json.optJSONObject("camera")?:SceneTemplates.camera()
    private fun setCamera(value:JSONObject){doc.json.put("camera",value);doc.markDirty();titleText?.text="${store.projectName} / ${store.sceneName} •"}
    private fun applyCamera(){val c=camera();val values=listOf("yaw","pitch","distance","x","y","z").map{key->
        val fallback=SceneTemplates.camera().getDouble(key);val v=c.optDouble(key,fallback);if(v.isFinite())v else fallback}
        val yaw=values[0].coerceIn(-360.0,360.0);val pitch=values[1].coerceIn(-80.0,80.0);val distance=values[2].coerceIn(1.5,40.0)
        NativeBridge.nativeExecConsole("orbit $yaw $pitch $distance ${values[3].coerceIn(-100.0,100.0)} ${values[4].coerceIn(-100.0,100.0)} ${values[5].coerceIn(-100.0,100.0)}")
    }
    private fun pan(dx:Float,dy:Float){val c=camera();val yaw=Math.toRadians(c.optDouble("yaw",30.0));c.put("x",(c.optDouble("x")+dx*cos(yaw)+dy*sin(yaw)).coerceIn(-100.0,100.0));c.put("z",(c.optDouble("z")-dx*sin(yaw)+dy*cos(yaw)).coerceIn(-100.0,100.0));setCamera(c);applyCamera()}
    private fun frameSelected(){val o=doc.selectedObject()?:return;val c=camera();c.put("x",o.getDouble("x"));c.put("y",o.getDouble("y"));c.put("z",o.getDouble("z"));c.put("distance",if(o.getString("kind")=="Terrain")14.0 else (o.optDouble("scale",1.0)*4).coerceIn(1.5,40.0));setCamera(c);applyCamera()}
    private fun sculpt(amount:Double){edit{next->val heights=next.optJSONArray("terrain")?:SceneTemplates.heights();for(z in 5..11)for(x in 5..11){val d=sqrt(((x-8)*(x-8)+(z-8)*(z-8)).toDouble());if(d<3.5){val i=z*17+x;heights.put(i,(heights.getDouble(i)+amount*(1-d/3.5)).coerceIn(-3.0,3.0))}};next.put("terrain",heights)}}
    private fun smoothTerrain(){edit{next->val old=next.optJSONArray("terrain")?:SceneTemplates.heights();val h=JSONArray(old.toString());for(z in 1..15)for(x in 1..15){var sum=0.0;for(dz in -1..1)for(dx in -1..1)sum+=old.getDouble((z+dz)*17+x+dx);h.put(z*17+x,sum/9)};next.put("terrain",h)}}
    private fun save():Boolean {return try{if(recoveryBlocked){store.newScene("Recovery ${System.currentTimeMillis()}",doc.json);recoveryBlocked=false};store.save(doc.json);store.save(doc.json,true);doc.saved();log("Saved ${store.projectName}/${store.sceneName}");titleText?.text="${store.projectName} / ${store.sceneName}";true}catch(e:Exception){log("Save failed: ${e.message}");toast("Save failed. Your current scene has been kept.");false}}
    fun saveRecovery(){if(!recoveryBlocked)safe{store.save(doc.json,true)}}
    fun trimMemory(){doc.trimHistory(8);while(logs.size>15)logs.removeFirst();NovaRuntime.targetFps=30;log("Low-memory mode: history trimmed, frame cap 30 FPS")}
    private fun open(value:JSONObject){if(playing)togglePlay();recoveryBlocked=false;doc=EditorDocument(value);NativeBridge.nativeSetScene(value.toString());applyCamera();refreshAll();log("Opened ${store.sceneName}")}
    private fun askName(title:String,default:String,action:(String)->Unit){val input=EditText(context).apply{setText(default);setSingleLine();selectAll()};AlertDialog.Builder(context).setTitle(title).setView(input).setPositiveButton("Create"){_,_->safe{val value=input.text.toString().trim();require(store.validName(value)){"Use letters, numbers, spaces, - or _ (1–40)"};action(value)}}.setNegativeButton("Cancel",null).show()}
    private fun newScene(){askName("Scene name","New Scene"){name->AlertDialog.Builder(context).setTitle("Scene template").setItems(arrayOf("Empty scene","Emerald Valley")){_,i->safe{if(save()){val value=if(i==0)SceneTemplates.blank()else SceneTemplates.island();store.newScene(name,value);open(value)}}}.show()}}
    private fun projects() {
        val entries=store.projects()
        AlertDialog.Builder(context).setTitle("Projects")
            .setItems((entries.map { it.second }+"+ Create project").toTypedArray()) { _, index ->
                safe {
                    if(index==entries.size) {
                        askName("Project name","New Project") { name ->
                            if(save()) { store.create(name);open(store.load()) }
                        }
                    } else if(save()) {
                        open(store.openProject(entries[index].first))
                    }
                }
            }.show()
    }
    fun importProject(text:String) {
        safe {
            if(recoveryBlocked || save()) {
                open(store.importProject(text))
                log("Imported validated JSON project")
            }
        }
    }
    private fun buildMenu(){output="ANDROID PROJECT CHECK\nProject: ${store.projectName}\nScene: ${store.sceneName}\nObjects: ${doc.objects.length()} / 32\nValidation: ${NativeBridge.nativeValidateScene(doc.json.toString())}\nRenderer: ${NativeBridge.nativeExecConsole("graphics")}\nTarget: Android 5+ / minSdk 21\n\nJSON export includes scenes, prefabs and console scripts. It is NOT an APK.\nAPK compilation requires the repository's Gradle/NDK GitHub Actions workflow. No on-device compiler is bundled."
        dockTab="Output";if(game)setGame(false);refreshDock()
        AlertDialog.Builder(context).setTitle("Build & export").setItems(arrayOf("Export project JSON…","Import project JSON…","Open GitHub APK build workflow","View build report")){_,i->safe{when(i){0->{if(save())exportRequested(store.exportProject())};1->importRequested();2->context.startActivity(Intent(Intent.ACTION_VIEW,Uri.parse("https://github.com/surafel5509-del/Unreal-Engine-Mobile/actions/workflows/android-apk.yml")));else->refreshDock()}}}.show()
    }
    private fun settings(){AlertDialog.Builder(context).setTitle("Workspace settings").setItems(arrayOf("Frame cap: 30 FPS (battery saver)","Frame cap: 60 FPS","Reset panel sizes","Project manager","About implemented features")){_,i->when(i){0,1->{val fps=if(i==0)30 else 60;NovaRuntime.targetFps=fps;prefs.edit().putInt("fps",fps).apply();log("Frame cap: $fps")};2->workspace.reset();3->projects();else->AlertDialog.Builder(context).setTitle("Nova Studio · Editor preview").setMessage("Working: scene editing, native picking, orbit/pan, procedural terrain, built-in meshes, color tint, visibility, rotation animation, prefabs, JSON import/export, console scripts, profiler.\n\nNot implemented: physical character controller, rigid-body physics, shadows, texture/model import, particles, audio, UI designer, gameplay scripting or on-device APK export. Vulkan is retained as an optional source backend; this compatibility APK uses GLES.").setPositiveButton("OK",null).show()}}.show()}
    private fun editScript(name:String){safe{val editor=EditText(context).apply{setText(runCatching{store.script(name)}.getOrDefault("# Native console commands\ngraphics\nstats"));typeface=Typeface.MONOSPACE;textSize=12f;gravity=Gravity.TOP;minLines=5;maxLines=8;tag="scriptEditor"}
        AlertDialog.Builder(context).setTitle("$name · Console script (not gameplay code)").setView(editor).setPositiveButton("Save"){_,_->safe{store.saveScript(name,editor.text.toString());log("Saved script: $name");refreshDock()}}.setNeutralButton("Run"){_,_->runCommands(editor.text.toString())}.setNegativeButton("Cancel",null).show()}}
    private fun runCommands(source:String){val lines=source.lines().filter{it.isNotBlank()&&!it.trim().startsWith("#")};if(lines.size>64){toast("Script limit: 64 commands");return};for(raw in lines){val line=raw.trim();when(line){"play"->{if(!playing)togglePlay()};"stop"->{if(playing)togglePlay()};else->log("> $line\n${NativeBridge.nativeExecConsole(line.take(1024))}")}}}
    private fun installGestures(){var lastX=0f;var lastY=0f;var startX=0f;var startY=0f;var drag=false;var edited=false;var pinched=false
        val pinch=ScaleGestureDetector(context,object:ScaleGestureDetector.SimpleOnScaleGestureListener(){override fun onScale(d:ScaleGestureDetector):Boolean{pinched=true;val c=camera();c.put("distance",(c.optDouble("distance",13.0)/d.scaleFactor).coerceIn(1.5,40.0));setCamera(c);applyCamera();return true}})
        viewport.touchSink={event->pinch.onTouchEvent(event);when(event.actionMasked){
            MotionEvent.ACTION_DOWN->{lastX=event.x;lastY=event.y;startX=event.x;startY=event.y;drag=true;edited=false;pinched=false}
            MotionEvent.ACTION_MOVE->if(drag&&!pinch.isInProgress&&!pinched&&event.pointerCount==1){val dx=event.x-lastX;val dy=event.y-lastY
                if(game||mode=="Orbit"){val c=camera();c.put("yaw",(c.optDouble("yaw",30.0)+dx*.25)%360);c.put("pitch",(c.optDouble("pitch",28.0)+dy*.2).coerceIn(-80.0,80.0));setCamera(c);applyCamera()}
                else if(!playing&&doc.selectedObject()!=null&&(abs(event.x-startX)+abs(event.y-startY)>ui.dp(4))){
                    val next=JSONObject(doc.json.toString());val o=next.getJSONArray("objects").getJSONObject(doc.selected)
                    val units=1.1547*camera().optDouble("distance",13.0)/max(1,viewport.height)
                    when(mode){"Move"->{val key=when(axis){"X"->"x";"Y"->"y";"Z"->"z";else->"x"};o.put(key,(o.getDouble(key)+(if(axis=="Y")-dy else dx)*units).coerceIn(-1000.0,1000.0));if(axis=="Free")o.put("y",(o.getDouble("y")-dy*units).coerceIn(-1000.0,1000.0))}
                        "Rotate"->{val key=when(axis){"X"->"rx";"Z"->"rz";else->"ry"};o.put(key,(o.getDouble(key)+dx*.5).coerceIn(-1000.0,1000.0))}
                        "Scale"->{val keys=when(axis){"X"->listOf("sx");"Y"->listOf("sy");"Z"->listOf("sz");else->listOf("sx","sy","sz")};for(k in keys)o.put(k,(o.optDouble(k,o.optDouble("scale",1.0))*exp(dx*.008)).coerceIn(.05,10.0))}}
                    if(next.toString()!=doc.json.toString()){if(!edited){doc.record();edited=true};change(next,false,false)}
                };lastX=event.x;lastY=event.y
            }
            MotionEvent.ACTION_UP->{if(!pinched&&!edited&&!game&&!playing&&abs(event.x-startX)+abs(event.y-startY)<ui.dp(8)){
                val index=NativeBridge.nativeExecConsole("pick ${(event.x/max(1,viewport.width)).coerceIn(0f,1f)} ${(event.y/max(1,viewport.height)).coerceIn(0f,1f)}").toIntOrNull()?:-1
                if(index>=0)doc.selected=index
            };drag=false;refreshAll()}
            MotionEvent.ACTION_CANCEL->{drag=false;refreshAll()}
        };true}
    }
    override fun onAttachedToWindow(){super.onAttachedToWindow();attached=true;post(ticker)}
    override fun onDetachedFromWindow(){attached=false;removeCallbacks(ticker);super.onDetachedFromWindow()}
}
