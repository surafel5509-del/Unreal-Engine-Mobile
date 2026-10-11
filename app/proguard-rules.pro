# Nova Engine ProGuard / R8 rules.
# Keep the JNI bridge: native methods are resolved by name at runtime.
-keepclasseswithmembers class com.nova.engine.NativeBridge {
    native <methods>;
}
-keep class com.nova.engine.NativeBridge { *; }

# Kotlin metadata (editor reflection uses it in later milestones).
-keepattributes *Annotation*, InnerClasses, Signature, SourceFile, LineNumberTable
