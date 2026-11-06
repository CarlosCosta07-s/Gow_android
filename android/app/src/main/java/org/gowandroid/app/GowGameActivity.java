package org.gowandroid.app;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.content.Intent;
import android.net.Uri;

import com.google.androidgamesdk.GameActivity;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Native entry point reserved for the God of War runtime.
 *
 * The Activity displays native startup diagnostics so an unavailable runtime
 * is not presented as an unexplained blank screen. It is not a playable game
 * runtime until private AOT code, GS/Vulkan presentation and Android audio are
 * integrated.
 */
public final class GowGameActivity extends GameActivity {
    public static final String EXTRA_DISC_FD =
            "org.gowandroid.app.extra.DISC_FD";
    public static final String EXTRA_PRIVATE_CD_ROOT =
            "org.gowandroid.app.extra.PRIVATE_CD_ROOT";
    public static final String EXTRA_BOOT_RELATIVE_PATH =
            "org.gowandroid.app.extra.BOOT_RELATIVE_PATH";

    private static native boolean nativeSetDiscFileDescriptor(int descriptor);
    private static native boolean nativeConfigurePrivateBoot(
            String privateCdRoot, String relativePath);
    private static native void nativeSetActivityReference(GowGameActivity activity);
    private static native void nativeClearActivityReference(GowGameActivity activity);
    private static native boolean nativeRequestGsSceneCapture();

    private static final int REQUEST_EXPORT_AOT_CAPTURES = 0x474F;
    private static final int REQUEST_EXPORT_RUNTIME_LOG = 0x4750;

    private TextView nativeStatusView;
    private String pendingNativeStatus;
    private boolean pendingNativeStatusError;

    static {
        System.loadLibrary("android-game");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Register before GameActivity starts android_main so the native thread
        // cannot observe a stale BOOT2 configuration from an earlier Activity.
        nativeSetActivityReference(this);
        super.onCreate(savedInstanceState);
        createNativeStatusOverlay();

        int discDescriptor = getIntent().getIntExtra(EXTRA_DISC_FD, -1);
        if (discDescriptor >= 0 && !nativeSetDiscFileDescriptor(discDescriptor)) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not initialize the native ISO reader from the supplied descriptor.");
        }

        String privateCdRoot = getIntent().getStringExtra(EXTRA_PRIVATE_CD_ROOT);
        String bootRelativePath = getIntent().getStringExtra(EXTRA_BOOT_RELATIVE_PATH);
        if (privateCdRoot != null && !privateCdRoot.isEmpty()
                && bootRelativePath != null && !bootRelativePath.isEmpty()) {
            if (nativeConfigurePrivateBoot(privateCdRoot, bootRelativePath)) {
                onNativeRuntimeStatus(
                        "Disco privado e BOOT2 encaminhados. Preparando o núcleo e verificando a cobertura AOT...",
                        false);
            } else {
                onNativeRuntimeStatus(
                        "Não foi possível montar a instalação privada ou registrar o BOOT2. O jogo não foi carregado.",
                        true);
            }
        } else {
            // Always release the native startup handshake, including diagnostic-only launches.
            nativeConfigurePrivateBoot("", "");
            onNativeRuntimeStatus(
                    "Nenhuma instalação/BOOT2 foi encaminhada pelo FORGE. Esta tela é somente diagnóstico.",
                    false);
        }

        getWindow().addFlags(
                android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    protected void onDestroy() {
        nativeClearActivityReference(this);
        super.onDestroy();
    }

    private void createNativeStatusOverlay() {
        ViewGroup content = findViewById(android.R.id.content);
        if (content == null) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not create native runtime status overlay: content view is missing.");
            return;
        }

        TextView status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(14.0f);
        status.setGravity(Gravity.CENTER_VERTICAL);
        status.setPadding(dp(16), dp(12), dp(16), dp(12));
        status.setMaxLines(5);
        status.setVisibility(View.GONE);
        status.setElevation(dp(16));
        status.setBackground(createStatusBackground(false));
        status.setContentDescription("Status de inicialização do runtime God of War");

        FrameLayout.LayoutParams layout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        layout.setMargins(dp(16), dp(20), dp(16), 0);
        content.addView(status, layout);
        nativeStatusView = status;

        if (pendingNativeStatus != null) {
            nativeStatusView.setText(pendingNativeStatus);
            nativeStatusView.setBackground(
                    createStatusBackground(pendingNativeStatusError));
            nativeStatusView.setVisibility(View.VISIBLE);
            pendingNativeStatus = null;
        }

        boolean isDebuggable = (getApplicationInfo().flags
                & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (isDebuggable) {
            Button captureButton = new Button(this);
            captureButton.setText("Capturar cena");
            captureButton.setAllCaps(false);
            captureButton.setContentDescription(
                    "Salvar o frame GS atual para comparação visual");
            captureButton.setOnClickListener(view -> {
                if (nativeRequestGsSceneCapture()) {
                    Toast.makeText(
                            this,
                            "Captura solicitada; será salva no próximo frame GS.",
                            Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(
                            this,
                            "Limite de capturas de depuração atingido.",
                            Toast.LENGTH_SHORT).show();
                }
            });
            FrameLayout.LayoutParams captureLayout = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.BOTTOM | Gravity.END);
            captureLayout.setMargins(0, 0, dp(16), dp(20));
            content.addView(captureButton, captureLayout);

            Button exportAotButton = new Button(this);
            exportAotButton.setText("Exportar dados AOT");
            exportAotButton.setAllCaps(false);
            exportAotButton.setContentDescription(
                    "Exportar as capturas privadas dos microprogramas VU1 e módulos IOP em um arquivo ZIP");
            exportAotButton.setOnClickListener(view -> beginAotCaptureExport());
            FrameLayout.LayoutParams exportLayout = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.BOTTOM | Gravity.START);
            exportLayout.setMargins(dp(16), 0, 0, dp(20));
            content.addView(exportAotButton, exportLayout);

            Button exportLogButton = new Button(this);
            exportLogButton.setText("Exportar log");
            exportLogButton.setAllCaps(false);
            exportLogButton.setTextSize(12.0f);
            exportLogButton.setContentDescription(
                    "Exportar o log privado e limitado da tentativa de boot para diagnóstico");
            exportLogButton.setOnClickListener(view -> beginRuntimeLogExport());
            FrameLayout.LayoutParams logLayout = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
            logLayout.setMargins(dp(8), 0, dp(8), dp(20));
            content.addView(exportLogButton, logLayout);
        }
    }

    private File aotCaptureRoot() {
        return new File(getFilesDir(), "gow-aot-captures");
    }

    private void beginAotCaptureExport() {
        File root = aotCaptureRoot();
        List<File> files = new ArrayList<>();
        collectAotCaptureFiles(root, root, files);
        if (files.isEmpty()) {
            Toast.makeText(
                    this,
                    "Ainda não há capturas VU1/IOP. Inicie a tentativa de boot com um ELF AOT válido.",
                    Toast.LENGTH_LONG).show();
            return;
        }

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        intent.putExtra(Intent.EXTRA_TITLE, "gow-aot-captures.zip");
        try {
            startActivityForResult(intent, REQUEST_EXPORT_AOT_CAPTURES);
        } catch (Exception exception) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not open the document picker for private AOT captures.",
                    exception);
            Toast.makeText(
                    this,
                    "Não foi possível abrir o seletor de destino.",
                    Toast.LENGTH_LONG).show();
        }
    }

    private void collectAotCaptureFiles(
            File root, File current, List<File> output) {
        if (current == null || !current.exists()
                || Files.isSymbolicLink(current.toPath())) {
            return;
        }
        if (current.isDirectory()) {
            File[] children = current.listFiles();
            if (children == null) return;
            java.util.Arrays.sort(children, Comparator.comparing(File::getName));
            for (File child : children) {
                collectAotCaptureFiles(root, child, output);
            }
            return;
        }
        if (current.isFile()) {
            String relative = root.toPath().relativize(current.toPath())
                    .toString().replace(File.separatorChar, '/');
            if (relative.startsWith("vu1/") || relative.startsWith("iop/")) {
                if (relative.endsWith(".bin")
                        || relative.equals("iop/modules.txt")
                        || relative.equals("vu1/vu1n_heads.txt")) {
                    output.add(current);
                }
            }
        }
    }

    private File privateRuntimeLogFile() {
        return new File(getFilesDir(), "gow-runtime.log");
    }

    private void beginRuntimeLogExport() {
        File logFile = privateRuntimeLogFile();
        if (!logFile.isFile() || Files.isSymbolicLink(logFile.toPath())) {
            Toast.makeText(
                    this,
                    "Ainda não há log privado de runtime. Abra Verificar runtime primeiro.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        if (logFile.length() > 2L * 1024L * 1024L) {
            Toast.makeText(
                    this,
                    "O log excedeu o limite de exportação e precisa ser inspecionado localmente.",
                    Toast.LENGTH_LONG).show();
            return;
        }

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, "gow-runtime.log");
        try {
            startActivityForResult(intent, REQUEST_EXPORT_RUNTIME_LOG);
        } catch (Exception exception) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not open the document picker for the private runtime log.",
                    exception);
            Toast.makeText(
                    this,
                    "Não foi possível abrir o seletor de destino do log.",
                    Toast.LENGTH_LONG).show();
        }
    }

    private void exportRuntimeLog(Uri destination) {
        File logFile = privateRuntimeLogFile();
        if (!logFile.isFile() || Files.isSymbolicLink(logFile.toPath())
                || logFile.length() > 2L * 1024L * 1024L) {
            Toast.makeText(
                    this,
                    "O log privado não está disponível ou excedeu o limite seguro.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        try {
            OutputStream raw = getContentResolver().openOutputStream(destination, "w");
            if (raw == null) throw new IOException("O provedor não abriu o destino do log.");
            try (BufferedInputStream input = new BufferedInputStream(
                        new FileInputStream(logFile));
                 BufferedOutputStream output = new BufferedOutputStream(raw)) {
                byte[] buffer = new byte[16 * 1024];
                int count;
                long copied = 0L;
                while ((count = input.read(buffer)) != -1) {
                    copied += count;
                    if (copied > 2L * 1024L * 1024L) {
                        throw new IOException("O log mudou durante a exportação e ultrapassou 2 MiB.");
                    }
                    output.write(buffer, 0, count);
                }
                output.flush();
            }
            Toast.makeText(
                    this,
                    "Log privado exportado. Mantenha o arquivo local e compartilhe somente para diagnóstico.",
                    Toast.LENGTH_LONG).show();
        } catch (Exception exception) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Private runtime log export failed.",
                    exception);
            Toast.makeText(
                    this,
                    "Falha ao exportar o log: " + exception.getMessage(),
                    Toast.LENGTH_LONG).show();
        }
    }

    private void exportAotCaptures(Uri destination) {
        if (destination == null) return;
        File root = aotCaptureRoot();
        List<File> files = new ArrayList<>();
        collectAotCaptureFiles(root, root, files);
        if (files.isEmpty()) {
            Toast.makeText(this, "Não há capturas AOT para exportar.", Toast.LENGTH_LONG).show();
            return;
        }

        long totalBytes = 0L;
        for (File file : files) {
            totalBytes += file.length();
        }
        if (files.size() > 8192 || totalBytes > 512L * 1024L * 1024L) {
            Toast.makeText(
                    this,
                    "O conjunto de capturas ultrapassa o limite seguro de exportação (512 MiB).",
                    Toast.LENGTH_LONG).show();
            return;
        }

        try {
            OutputStream raw = getContentResolver().openOutputStream(destination, "w");
            if (raw == null) throw new IOException("O provedor não abriu o destino ZIP.");
            long copiedBytes = 0L;
            int vu1Files = 0;
            int iopFiles = 0;
            try (ZipOutputStream zip = new ZipOutputStream(
                    new BufferedOutputStream(raw))) {
                byte[] buffer = new byte[64 * 1024];
                for (File file : files) {
                    String relative = root.toPath().relativize(file.toPath())
                            .toString().replace(File.separatorChar, '/');
                    ZipEntry entry = new ZipEntry(relative);
                    zip.putNextEntry(entry);
                    try (BufferedInputStream input = new BufferedInputStream(
                            new FileInputStream(file))) {
                        int count;
                        while ((count = input.read(buffer)) != -1) {
                            zip.write(buffer, 0, count);
                            copiedBytes += count;
                        }
                    }
                    zip.closeEntry();
                    if (relative.startsWith("vu1/") && relative.endsWith(".bin")) vu1Files++;
                    if (relative.startsWith("iop/") && relative.endsWith(".bin")) iopFiles++;
                }

                String manifest =
                        "format=gow-native-aot-captures-v1\n"
                        + "vu1_microcode_files=" + vu1Files + "\n"
                        + "iop_module_files=" + iopFiles + "\n"
                        + "captured_bytes=" + copiedBytes + "\n"
                        + "private_user_data=true\n"
                        + "Do not upload this archive to public repositories or CI.\n";
                zip.putNextEntry(new ZipEntry("capture_manifest.txt"));
                zip.write(manifest.getBytes(StandardCharsets.UTF_8));
                zip.closeEntry();
                zip.finish();
            }

            Toast.makeText(
                    this,
                    "Capturas AOT exportadas: " + vu1Files + " VU1, "
                            + iopFiles + " módulos IOP.",
                    Toast.LENGTH_LONG).show();
        } catch (Exception exception) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Private AOT capture export failed.",
                    exception);
            Toast.makeText(
                    this,
                    "Falha ao exportar capturas AOT: " + exception.getMessage(),
                    Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) return;
        Uri destination = data.getData();
        if (destination == null) return;
        if (requestCode == REQUEST_EXPORT_AOT_CAPTURES) {
            exportAotCaptures(destination);
        } else if (requestCode == REQUEST_EXPORT_RUNTIME_LOG) {
            exportRuntimeLog(destination);
        }
    }

    private GradientDrawable createStatusBackground(boolean isError) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(isError ? 0xE64A171B : 0xE6222730);
        background.setCornerRadius(dp(12));
        background.setStroke(dp(1), isError ? 0xFFFF8585 : 0xFF596574);
        return background;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    /**
     * Called by the Android native-app thread through JNI. UI changes are
     * always posted to the Activity's main thread.
     */
    public void onNativeRuntimeStatus(String message, boolean isError) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) {
                return;
            }
            if (nativeStatusView == null) {
                pendingNativeStatus = message == null ? "" : message;
                pendingNativeStatusError = isError;
                return;
            }
            nativeStatusView.setText(message == null ? "" : message);
            nativeStatusView.setBackground(createStatusBackground(isError));
            nativeStatusView.setVisibility(View.VISIBLE);
        });
    }
}
