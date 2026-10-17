plugins { id("org.jetbrains.kotlin.jvm"); id("com.google.protobuf") }
kotlin {
    jvmToolchain(21)
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}
java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}
sourceSets { main { proto { srcDir("../../protocol") } } }
protobuf {
    protoc { artifact = "com.google.protobuf:protoc:4.33.5" }
    generateProtoTasks { all().configureEach { builtins { named("java") { option("lite") } } } }
}
dependencies {
    implementation("com.google.protobuf:protobuf-javalite:4.33.5")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    testImplementation("junit:junit:4.13.2")
}
