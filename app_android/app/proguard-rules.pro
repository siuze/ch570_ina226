# Maximum R8 optimization rules for CH570 Probe Monitor.
-repackageclasses ''
-allowaccessmodification

# Keep Android entry points and explicitly retained annotations.
-keepattributes *Annotation*
-keepclassmembers class * {
    @androidx.annotation.Keep *;
}
