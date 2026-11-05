package org.gowandroid.app;

import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * User-facing launcher for the Android native port.
 *
 * At this milestone the launcher only validates the ISO9660 primary volume
 * descriptor. It does not identify the God of War serial or boot the game.
 */
public final class MainActivity extends AppCompatActivity {
    private static final String PREFS_NAME = "gow_android";
    private static final String PREF_DISC_URI = "selected_disc_uri";
    private static final int ISO_SECTOR_SIZE = 2048;
    private static final long PRIMARY_VOLUME_DESCRIPTOR_OFFSET = 16L * ISO_SECTOR_SIZE;

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();

    private TextView discStatus;
    private TextView discName;
    private Button selectDiscButton;

    private final ActivityResultLauncher<String[]> openDocumentLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.OpenDocument(),
                    this::onDiscSelected);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildLauncherUi();
        restoreSelectedDisc();
    }

    private void buildLauncherUi() {
        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.setBackgroundColor(Color.rgb(12, 10, 15));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        int horizontalPadding = dp(24);
        content.setPadding(horizontalPadding, dp(44), horizontalPadding, dp(32));

        TextView title = new TextView(this);
        title.setText("GOD OF WAR");
        title.setTextColor(Color.rgb(224, 207, 174));
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        title.setLetterSpacing(0.12f);
        content.addView(title, matchWrap());

        TextView subtitle = new TextView(this);
        subtitle.setText("ANDROID NATIVE PORT");
        subtitle.setTextColor(Color.rgb(167, 158, 150));
        subtitle.setTextSize(12);
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setLetterSpacing(0.18f);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(8);
        content.addView(subtitle, subtitleParams);

        TextView section = new TextView(this);
        section.setText("Imagem do disco");
        section.setTextColor(Color.WHITE);
        section.setTextSize(18);
        LinearLayout.LayoutParams sectionParams = matchWrap();
        sectionParams.topMargin = dp(40);
        content.addView(section, sectionParams);

        discName = new TextView(this);
        discName.setText("Nenhuma imagem selecionada");
        discName.setTextColor(Color.rgb(205, 199, 190));
        discName.setTextSize(14);
        discName.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams nameParams = matchWrap();
        nameParams.topMargin = dp(14);
        content.addView(discName, nameParams);

        discStatus = new TextView(this);
        discStatus.setText("Selecione uma imagem para verificar o formato ISO9660.");
        discStatus.setTextColor(Color.rgb(170, 164, 157));
        discStatus.setTextSize(13);
        discStatus.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = matchWrap();
        statusParams.topMargin = dp(10);
        content.addView(discStatus, statusParams);

        selectDiscButton = new Button(this);
        selectDiscButton.setText("Selecionar imagem do disco");
        selectDiscButton.setAllCaps(false);
        selectDiscButton.setOnClickListener(
                view -> openDocumentLauncher.launch(new String[]{"*/*"}));
        LinearLayout.LayoutParams selectParams = matchWrap();
        selectParams.topMargin = dp(28);
        content.addView(selectDiscButton, selectParams);

        Button vulkanButton = new Button(this);
        vulkanButton.setText("Executar teste Vulkan");
        vulkanButton.setAllCaps(false);
        vulkanButton.setOnClickListener(
                view -> startActivity(new Intent(this, GowGameActivity.class)));
        LinearLayout.LayoutParams vulkanParams = matchWrap();
        vulkanParams.topMargin = dp(12);
        content.addView(vulkanButton, vulkanParams);

        TextView explanation = new TextView(this);
        explanation.setText(
                "O teste Vulkan verifica a apresentação gráfica do Android e não inicia o jogo. "
                        + "A validação atual confirma apenas a presença de um descritor ISO9660; "
                        + "a identificação da revisão de God of War e a execução do jogo ainda "
                        + "não foram implementadas. O aplicativo não copia nem envia sua imagem "
                        + "de disco.");
        explanation.setTextColor(Color.rgb(145, 139, 133));
        explanation.setTextSize(12);
        explanation.setLineSpacing(dp(4), 1.0f);
        LinearLayout.LayoutParams explanationParams = matchWrap();
        explanationParams.topMargin = dp(30);
        content.addView(explanation, explanationParams);

        TextView buildLabel = new TextView(this);
        buildLabel.setText("SHELL DE DESENVOLVIMENTO • ARM64 • VULKAN");
        buildLabel.setTextColor(Color.rgb(112, 106, 101));
        buildLabel.setTextSize(10);
        buildLabel.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams buildParams = matchWrap();
        buildParams.topMargin = dp(36);
        content.addView(buildLabel, buildParams);

        scrollView.addView(content);
        setContentView(scrollView);
    }

    private void onDiscSelected(Uri uri) {
        if (uri == null) {
            return;
        }

        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some document providers grant only temporary access. The
            // current validation still works while this Activity is alive.
        }

        getPreferences(MODE_PRIVATE)
                .edit()
                .putString(PREF_DISC_URI, uri.toString())
                .apply();

        validateAndDisplay(uri);
    }

    private void restoreSelectedDisc() {
        SharedPreferences preferences = getPreferences(MODE_PRIVATE);
        String savedUri = preferences.getString(PREF_DISC_URI, null);
        if (savedUri == null || savedUri.isEmpty()) {
            return;
        }
        validateAndDisplay(Uri.parse(savedUri));
    }

    private void validateAndDisplay(Uri uri) {
        selectDiscButton.setEnabled(false);
        discStatus.setText("Verificando o descritor ISO9660...");
        discStatus.setTextColor(Color.rgb(220, 190, 130));

        ioExecutor.execute(() -> {
            String name = queryDisplayName(uri);
            String result;
            boolean valid = false;

            try {
                validateIso9660(uri);
                valid = true;
                result = "Descritor ISO9660 encontrado. Serial e revisão do jogo ainda não verificados.";
            } catch (IOException | SecurityException exception) {
                result = "Não foi possível validar a imagem: " + safeMessage(exception);
            }

            final boolean isValid = valid;
            final String finalName = name;
            final String finalResult = result;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                discName.setText(finalName);
                discStatus.setText(finalResult);
                discStatus.setTextColor(
                        isValid ? Color.rgb(143, 196, 151) : Color.rgb(220, 135, 125));
                selectDiscButton.setEnabled(true);
            });
        });
    }

    private void validateIso9660(Uri uri) throws IOException {
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) {
                throw new IOException("O provedor não conseguiu abrir o arquivo.");
            }

            skipFully(input, PRIMARY_VOLUME_DESCRIPTOR_OFFSET);
            byte[] descriptor = new byte[ISO_SECTOR_SIZE];
            readFully(input, descriptor);

            if ((descriptor[0] & 0xFF) != 1
                    || descriptor[1] != 'C'
                    || descriptor[2] != 'D'
                    || descriptor[3] != '0'
                    || descriptor[4] != '0'
                    || descriptor[5] != '1'
                    || (descriptor[6] & 0xFF) != 1) {
                throw new IOException(
                        "O setor 16 não contém um descritor primário ISO9660 válido.");
            }
        }
    }

    private static void skipFully(InputStream input, long byteCount) throws IOException {
        long remaining = byteCount;
        while (remaining > 0) {
            long skipped = input.skip(remaining);
            if (skipped > 0) {
                remaining -= skipped;
                continue;
            }

            if (input.read() == -1) {
                throw new IOException("Arquivo muito pequeno para conter o descritor ISO9660.");
            }
            remaining--;
        }
    }

    private static void readFully(InputStream input, byte[] buffer) throws IOException {
        int offset = 0;
        while (offset < buffer.length) {
            int count = input.read(buffer, offset, buffer.length - offset);
            if (count < 0) {
                throw new IOException("Fim inesperado do arquivo ao ler o descritor ISO9660.");
            }
            if (count == 0) {
                continue;
            }
            offset += count;
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    String value = cursor.getString(index);
                    if (value != null && !value.trim().isEmpty()) {
                        return value;
                    }
                }
            }
        } catch (RuntimeException ignored) {
            // A document provider may not expose a display name.
        }
        return "Imagem de disco selecionada";
    }

    private static String safeMessage(Exception exception) {
        String message = exception.getMessage();
        return message == null || message.trim().isEmpty()
                ? exception.getClass().getSimpleName()
                : message;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onDestroy() {
        ioExecutor.shutdownNow();
        super.onDestroy();
    }
}
