package com.nova.engine.editor

import android.content.Context
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import kotlin.math.roundToInt

/** A single persistent SurfaceView stays attached while users drag any splitter. */
class SplitWorkspace(context:Context,val left:View,val center:View,val right:View,val bottom:View):ViewGroup(context) {
    private val ui=EditorUi(context)
    private val prefs=context.getSharedPreferences("studio-layout",Context.MODE_PRIVATE)
    private var leftDp=prefs.getInt("left",110);private var rightDp=prefs.getInt("right",180);private var dockDp=prefs.getInt("dock",100)
    private var lw=0;private var rw=0;private var dh=0;private var topH=0
    var game=false;set(value){field=value;requestLayout()}
    private fun splitter(name:String,change:(Float,Float)->Unit)=View(context).apply {
        tag=name;contentDescription="Resize $name panel";setBackgroundColor(StudioColors.border)
        var x=0f;var y=0f
        setOnTouchListener { _,e ->
            when(e.actionMasked){MotionEvent.ACTION_DOWN->{x=e.rawX;y=e.rawY;parent.requestDisallowInterceptTouchEvent(true)}
                MotionEvent.ACTION_MOVE->{change(e.rawX-x,e.rawY-y);x=e.rawX;y=e.rawY;requestLayout()}
                MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL->{prefs.edit().putInt("left",leftDp).putInt("right",rightDp).putInt("dock",dockDp).apply()}}
            true
        }
    }
    private val a=splitter("hierarchy"){dx,_->leftDp+=(dx/resources.displayMetrics.density).roundToInt()}
    private val b=splitter("inspector"){dx,_->rightDp-=(dx/resources.displayMetrics.density).roundToInt()}
    private val c=splitter("assets"){_,dy->dockDp-=(dy/resources.displayMetrics.density).roundToInt()}
    init {for(v in listOf(left,center,right,bottom,a,b,c))addView(v)}
    fun reset(){leftDp=110;rightDp=180;dockDp=100;prefs.edit().clear().apply();requestLayout()}
    override fun onMeasure(w:Int,h:Int){
        val width=MeasureSpec.getSize(w);val height=MeasureSpec.getSize(h);val gap=ui.dp(8)
        val available=width-2*gap;val minCenter=minOf(ui.dp(200),available/2)
        lw=ui.dp(leftDp.coerceIn(90,250)).coerceAtMost((available-minCenter)*2/5)
        rw=ui.dp(rightDp.coerceIn(145,300)).coerceAtMost(available-minCenter-lw)
        dh=ui.dp(dockDp.coerceIn(64,260)).coerceAtMost((height-ui.dp(130)).coerceAtLeast(ui.dp(50)))
        leftDp=(lw/resources.displayMetrics.density).roundToInt();rightDp=(rw/resources.displayMetrics.density).roundToInt();dockDp=(dh/resources.displayMetrics.density).roundToInt()
        topH=(height-dh-gap).coerceAtLeast(1)
        for(v in listOf(left,right,bottom,a,b,c))v.visibility=if(game)GONE else VISIBLE
        fun measure(v:View,x:Int,y:Int)=v.measure(MeasureSpec.makeMeasureSpec(x.coerceAtLeast(1),MeasureSpec.EXACTLY),MeasureSpec.makeMeasureSpec(y.coerceAtLeast(1),MeasureSpec.EXACTLY))
        if(game)measure(center,width,height) else {measure(left,lw,topH);measure(center,width-lw-rw-2*gap,topH);measure(right,rw,height);measure(bottom,width-rw-gap,dh);measure(a,gap,topH);measure(b,gap,height);measure(c,width-rw-gap,gap)}
        setMeasuredDimension(width,height)
    }
    override fun onLayout(changed:Boolean,l:Int,t:Int,r:Int,bottomEdge:Int){
        val w=r-l;val h=bottomEdge-t;val gap=ui.dp(8)
        if(game){center.layout(0,0,w,h);return}
        left.layout(0,0,lw,topH);a.layout(lw,0,lw+gap,topH)
        center.layout(lw+gap,0,w-rw-gap,topH);b.layout(w-rw-gap,0,w-rw,h);right.layout(w-rw,0,w,h)
        c.layout(0,topH,w-rw-gap,topH+gap);bottom.layout(0,topH+gap,w-rw-gap,h)
    }
}
