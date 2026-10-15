package com.nova.engine.editor

import android.content.Context
import android.graphics.*
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.*
import kotlin.math.*

object StudioColors {
    val background=Color.rgb(12,19,29)
    val panel=Color.rgb(20,30,43)
    val raised=Color.rgb(28,41,57)
    val border=Color.rgb(36,51,69)
    val text=Color.rgb(223,234,246)
    val muted=Color.rgb(135,157,181)
    val blue=Color.rgb(77,167,255)
    val selected=Color.rgb(23,74,124)
}
class EditorUi(val context:Context) {
    fun dp(n:Int)=(n*context.resources.displayMetrics.density).toInt()
    fun text(value:String,size:Float=12f,color:Int=StudioColors.text)=TextView(context).apply {
        text=value; textSize=size; setTextColor(color); gravity=Gravity.CENTER_VERTICAL
        setPadding(dp(8),dp(3),dp(8),dp(3)); fontFeatureSettings="kern"
    }
    fun column()=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL; setBackgroundColor(StudioColors.panel)}
    fun row()=LinearLayout(context).apply {orientation=LinearLayout.HORIZONTAL; gravity=Gravity.CENTER_VERTICAL}
    fun shape(color:Int,radius:Int=4)=GradientDrawable().apply {setColor(color); cornerRadius=dp(radius).toFloat();setStroke(dp(1),StudioColors.border)}
    fun scroll(child:View)=ScrollView(context).apply {isFillViewport=true;addView(child)}
    fun horizontal(child:View)=HorizontalScrollView(context).apply {isHorizontalScrollBarEnabled=false;addView(child)}
    fun button(label:String,key:String=label,icon:String?=null,action:()->Unit):LinearLayout = row().apply {
        tag=key; contentDescription=label; isClickable=true; isFocusable=true
        background=shape(StudioColors.raised); minimumHeight=dp(40); setPadding(dp(4),0,dp(4),0)
        if(icon!=null) addView(StudioIcon(context,icon),LinearLayout.LayoutParams(dp(22),dp(22)))
        if(label.isNotEmpty()) addView(text(label,11f))
        layoutParams=LinearLayout.LayoutParams(-2,dp(40)).apply {setMargins(dp(2),dp(2),dp(2),dp(2))}
        setOnClickListener {action()}
    }
    fun title(parent:LinearLayout,value:String) { parent.addView(text(value,11f).apply {typeface=Typeface.create("sans-serif-medium",0);setBackgroundColor(StudioColors.raised)},LinearLayout.LayoutParams(-1,dp(30))) }
}

/** Small vector icon set, drawn at device density; no bitmap atlas allocations. */
class StudioIcon(context:Context,private val kind:String,var tint:Int=StudioColors.muted):View(context) {
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG)
    override fun onDraw(canvas:Canvas) {
        super.onDraw(canvas); canvas.save();canvas.translate(width*.12f,height*.12f);canvas.scale(width*.76f/24,height*.76f/24)
        paint.color=tint;paint.style=Paint.Style.STROKE;paint.strokeWidth=1.7f;paint.strokeCap=Paint.Cap.ROUND;paint.strokeJoin=Paint.Join.ROUND
        fun line(x:Float,y:Float,a:Float,b:Float)=canvas.drawLine(x,y,a,b,paint)
        fun path(vararg xy:Float) {val p=Path();p.moveTo(xy[0],xy[1]); for(i in 2 until xy.size step 2)p.lineTo(xy[i],xy[i+1]);canvas.drawPath(p,paint)}
        when(kind.lowercase()) {
            "play" -> {paint.style=Paint.Style.FILL;path(7f,3f,21f,12f,7f,21f,7f,3f)}
            "stop" -> {paint.style=Paint.Style.FILL;canvas.drawRoundRect(5f,5f,19f,19f,2f,2f,paint)}
            "scene","cube","prefab" -> {path(12f,1f,22f,6f,22f,18f,12f,23f,2f,18f,2f,6f,12f,1f);path(2f,6f,12f,12f,22f,6f);line(12f,12f,12f,23f)}
            "sphere" -> {canvas.drawCircle(12f,12f,10f,paint);canvas.drawOval(7f,2f,17f,22f,paint);canvas.drawOval(2f,8f,22f,16f,paint)}
            "game" -> {canvas.drawRoundRect(2f,6f,22f,20f,4f,4f,paint);line(5f,12f,11f,12f);line(8f,9f,8f,15f);canvas.drawCircle(17f,11f,1f,paint);canvas.drawCircle(19f,15f,1f,paint)}
            "build","export","import" -> {path(4f,9f,4f,22f,20f,22f,20f,9f);line(12f,2f,12f,15f);if(kind=="import")path(8f,11f,12f,15f,16f,11f) else path(8f,6f,12f,2f,16f,6f)}
            "save" -> {path(3f,2f,18f,2f,22f,6f,22f,22f,3f,22f,3f,2f);canvas.drawRect(7f,2f,16f,9f,paint);canvas.drawRect(7f,14f,18f,22f,paint)}
            "undo","redo" -> {if(kind=="redo") {canvas.translate(24f,0f);canvas.scale(-1f,1f)};canvas.drawArc(5f,7f,22f,23f,220f,230f,false,paint);path(3f,3f,3f,11f,11f,11f)}
            "settings" -> {canvas.drawCircle(12f,12f,6f,paint);canvas.drawCircle(12f,12f,2f,paint);for(i in 0..7){val a=i*PI/4;line((12+7*cos(a)).toFloat(),(12+7*sin(a)).toFloat(),(12+10*cos(a)).toFloat(),(12+10*sin(a)).toFloat())}}
            "move" -> {line(2f,12f,22f,12f);line(12f,2f,12f,22f);path(8f,6f,12f,2f,16f,6f);path(8f,18f,12f,22f,16f,18f);path(6f,8f,2f,12f,6f,16f);path(18f,8f,22f,12f,18f,16f)}
            "rotate","orbit" -> {canvas.drawArc(3f,3f,21f,21f,30f,300f,false,paint);path(16f,2f,22f,4f,20f,10f)}
            "scale" -> {canvas.drawRect(2f,12f,12f,22f,paint);path(12f,12f,22f,2f,15f,2f);line(22f,2f,22f,9f)}
            "folder","project" -> path(2f,6f,10f,6f,12f,9f,22f,9f,22f,21f,2f,21f,2f,6f)
            "tree" -> {path(12f,1f,3f,16f,21f,16f,12f,1f);line(12f,16f,12f,23f)}
            "house" -> {path(1f,11f,12f,2f,23f,11f);path(4f,9f,4f,22f,20f,22f,20f,9f);canvas.drawRect(10f,14f,15f,22f,paint)}
            "terrain","cone","triangle" -> {path(2f,21f,10f,4f,17f,16f,20f,11f,24f,21f,2f,21f)}
            "plane" -> path(1f,15f,15f,5f,23f,10f,9f,22f,1f,15f)
            "eye" -> {canvas.drawOval(1f,6f,23f,18f,paint);canvas.drawCircle(12f,12f,3f,paint)}
            "delete" -> {line(3f,5f,21f,5f);path(6f,5f,7f,22f,17f,22f,18f,5f);path(9f,5f,9f,2f,15f,2f,15f,5f)}
            "duplicate" -> {canvas.drawRect(2f,2f,16f,16f,paint);canvas.drawRect(8f,8f,22f,22f,paint)}
            "console","script" -> {path(3f,5f,10f,12f,3f,19f);line(12f,19f,22f,19f)}
            "search" -> {canvas.drawCircle(10f,10f,7f,paint);line(15f,15f,22f,22f)}
            "plus" -> {line(12f,3f,12f,21f);line(3f,12f,21f,12f)}
            else -> {for(y in listOf(5f,12f,19f)) line(3f,y,21f,y)}
        }
        canvas.restore()
    }
}

/** Navigation-only joystick: pans the preview camera, not a pretend character controller. */
class NavigationPad(context:Context,private val moved:(Float,Float)->Unit):View(context) {
    private val p=Paint(Paint.ANTI_ALIAS_FLAG); private var dx=0f;private var dy=0f;private var held=false
    private val tick=object:Runnable {override fun run(){if(held){moved(dx,dy);postDelayed(this,32)}}}
    init {contentDescription="Preview camera navigation joystick";tag="joystick"}
    override fun onDraw(c:Canvas) {val r=min(width,height)*.45f;p.color=0x77344558;c.drawCircle(width/2f,height/2f,r,p);p.color=0xaa81bfff.toInt();c.drawCircle(width/2f+dx*r*.6f,height/2f+dy*r*.6f,r*.32f,p)}
    override fun onTouchEvent(e:MotionEvent):Boolean {
        when(e.actionMasked){MotionEvent.ACTION_DOWN->{held=true;post(tick)};MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL->{held=false;dx=0f;dy=0f;removeCallbacks(tick);invalidate();return true}}
        val r=min(width,height)*.45f;dx=((e.x-width/2)/r).coerceIn(-1f,1f);dy=((e.y-height/2)/r).coerceIn(-1f,1f);invalidate();return true
    }
    override fun onDetachedFromWindow(){held=false;removeCallbacks(tick);super.onDetachedFromWindow()}
}
