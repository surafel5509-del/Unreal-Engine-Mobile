package com.nova.engine

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.util.Log
import java.io.File
import java.io.IOException

/**
 * First-launch asset materialization.
 *
 * The native engine reads immutable assets (shaders, scenes) directly from the
 * APK through AAssetManager, but some subsystems need a writable working tree
 * (log files, cooked-asset cache, editor hot-reload staging). This class copies
 * `src/main/assets/` into `<filesDir>/nova/` once per app version and returns
 * the path, which is handed to the native layer as its data directory.
 *
 * Re-running [prepare] after an app update rebuilds the tree atomically enough
 * for our purposes: the version marker is written last, so a partially copied
 * tree is always redone on the next launch.
 */
object NovaAssetManager {
    private const val TAG = "NovaAssets"
    private const val ROOT_DIR = "nova"
    private const val MARKER = ".version"

    /** Ensure the writable asset tree exists; returns the data root. */
    @JvmStatic
    fun prepare(context: Context): File {
        val root = File(context.filesDir, ROOT_DIR)
        val marker = File(root, MARKER)
        val version = appVersion(context)

        if (marker.isFile && marker.readText() == version && File(root, "logs").isDirectory) {
            return root
        }

        Log.i(TAG, "Materializing assets (version $version) into $root")
        if (root.exists() && !root.deleteRecursively()) {
            Log.w(TAG, "Could not fully clean $root; continuing")
        }
        if (!root.mkdirs()) {
            throw IOException("Unable to create engine data directory: $root")
        }

        copyAssets(context, "", root)
        File(root, "logs").mkdirs()
        marker.writeText(version)
        return root
    }

    private fun appVersion(context: Context): String = try {
        val info: PackageInfo = context.packageManager.getPackageInfo(context.packageName, 0)
        info.versionName ?: "0"
    } catch (e: PackageManager.NameNotFoundException) {
        Log.w(TAG, "Unable to read package version, using '0'", e)
        "0"
    }

    private fun copyAssets(context: Context, prefix: String, dest: File) {
        val names = context.assets.list(prefix) ?: return
        for (name in names) {
            val assetPath = if (prefix.isEmpty()) name else "$prefix/$name"
            val children = context.assets.list(assetPath)
            val target = File(dest, name)
            if (children != null && children.isNotEmpty()) {
                if (!target.mkdirs() && !target.isDirectory) {
                    throw IOException("Unable to create directory $target")
                }
                copyAssets(context, assetPath, target)
            } else {
                context.assets.open(assetPath).use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                }
            }
        }
    }
}

