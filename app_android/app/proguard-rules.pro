# Proguard rules for CH570 Probe Monitor
-keepattributes *Annotation*
-keepclassmembers class * {
    @androidx.annotation.Keep *;
}
