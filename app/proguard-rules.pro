-keep class com.yambz.** { *; }
-keep class qing.albatross.** { *; }
-keepclassmembers class * {
    native <methods>;
}
-keepattributes *Annotation*
