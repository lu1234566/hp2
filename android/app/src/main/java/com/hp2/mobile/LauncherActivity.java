package com.hp2.mobile;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import java.util.Locale;

public final class LauncherActivity extends AppCompatActivity {
    private static final int REQUEST_GAME_FOLDER = 1001;
    private static final int REQUEST_GAME_ARCHIVE = 1002;
    private static final int EXPECTED_PACKAGES = 107;

    private static final int COLOR_BACKGROUND = Color.rgb(9, 12, 22);
    private static final int COLOR_CARD = Color.rgb(29, 34, 48);
    private static final int COLOR_CARD_ALT = Color.rgb(35, 41, 57);
    private static final int COLOR_TEXT = Color.rgb(239, 242, 246);
    private static final int COLOR_MUTED = Color.rgb(174, 181, 196);
    private static final int COLOR_GREEN = Color.rgb(117, 220, 151);
    private static final int COLOR_GREEN_DARK = Color.rgb(24, 94, 57);
    private static final int COLOR_GOLD = Color.rgb(218, 180, 91);

    private GameDataRepository repository;
    private TextView dataState;
    private TextView packageState;
    private TextView importNote;
    private ProgressBar progress;
    private Button launchButton;
    private Button folderButton;
    private Button archiveButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        repository = new GameDataRepository(this);
        setContentView(buildContent());
        refreshData();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (repository != null) {
            refreshData();
        }
    }

    @Override
    protected void onDestroy() {
        if (repository != null) {
            repository.close();
        }
        super.onDestroy();
    }

    private View buildContent() {
        final ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(COLOR_BACKGROUND);

        final LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(22), dp(20), dp(22), dp(32));
        scroll.addView(content, new ScrollView.LayoutParams(
            ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT
        ));

        final LinearLayout titleRow = new LinearLayout(this);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        final TextView title = text("HP2 Mobile", 31, COLOR_TEXT, Typeface.BOLD);
        titleRow.addView(title, new LinearLayout.LayoutParams(0, dp(56), 1f));
        final Button info = compactButton("Sobre");
        info.setOnClickListener(view -> Toast.makeText(this,
            "Port clean-room. Nenhum arquivo do jogo é incluído no APK.", Toast.LENGTH_LONG).show());
        titleRow.addView(info, new LinearLayout.LayoutParams(dp(96), dp(48)));
        content.addView(titleRow);

        content.addView(space(14));
        final LinearLayout hero = card(COLOR_GREEN_DARK, 26);
        final TextView gate = text("G5b · inspeção do SkeletalMesh", 14, COLOR_GREEN, Typeface.BOLD);
        hero.addView(gate);
        hero.addView(space(10));
        dataState = text("Verificando dados…", 24, COLOR_TEXT, Typeface.BOLD);
        hero.addView(dataState);
        hero.addView(space(6));
        packageState = text("", 15, Color.rgb(203, 232, 211), Typeface.NORMAL);
        hero.addView(packageState);
        hero.addView(space(20));
        launchButton = primaryButton("Abrir runtime nativo");
        launchButton.setOnClickListener(view -> startActivity(new Intent(this, MainActivity.class)));
        hero.addView(launchButton, matchWidth(dp(58)));
        content.addView(hero);

        content.addView(space(18));
        content.addView(sectionTitle("Dados do jogo"));
        content.addView(space(10));

        final LinearLayout dataCard = card(COLOR_CARD, 22);
        dataCard.addView(text("Use sua cópia original instalada", 20, COLOR_TEXT, Typeface.BOLD));
        dataCard.addView(space(6));
        dataCard.addView(text(
            "Selecione a pasta que contém Maps, Textures, System, Sounds e Music, ou um ZIP dessas pastas.",
            15, COLOR_MUTED, Typeface.NORMAL
        ));
        dataCard.addView(space(18));
        folderButton = secondaryButton("Importar pasta instalada");
        folderButton.setOnClickListener(view -> chooseGameFolder());
        dataCard.addView(folderButton, matchWidth(dp(54)));
        dataCard.addView(space(10));
        archiveButton = secondaryButton("Importar ZIP / reconhecer MDF");
        archiveButton.setOnClickListener(view -> chooseArchive());
        dataCard.addView(archiveButton, matchWidth(dp(54)));
        dataCard.addView(space(14));
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        progress.setVisibility(View.GONE);
        dataCard.addView(progress, matchWidth(dp(5)));
        dataCard.addView(space(10));
        importNote = text(
            "O MDF original é reconhecido, mas a extração InstallShield ainda não faz parte desta compilação.",
            13, COLOR_GOLD, Typeface.NORMAL
        );
        dataCard.addView(importNote);
        content.addView(dataCard);

        content.addView(space(18));
        content.addView(sectionTitle("Marcos do port"));
        content.addView(space(10));
        content.addView(gateCard("G0", "Runtime nativo + controle externo", "PASS", COLOR_GREEN));
        content.addView(space(10));
        content.addView(gateCard("G1", "107/107 pacotes originais catalogados", "PASS", COLOR_GREEN));
        content.addView(space(10));
        content.addView(gateCard("G2", "Duel10: BSP real no Galaxy A57", "PASS", COLOR_GREEN));
        content.addView(space(10));
        content.addView(gateCard("G3", "UV + primeira textura no Galaxy A57", "PASS", COLOR_GREEN));
        content.addView(space(10));
        content.addView(gateCard("G4", "Texturas BSP + máscaras de luz", "PASS", COLOR_GREEN));
        content.addView(space(10));
        content.addView(gateCard("G5", "Pose-base + foco com botão A", "EM TESTE", COLOR_GOLD));

        content.addView(space(20));
        final Button appSettings = compactButton("Abrir informações do app");
        appSettings.setOnClickListener(view -> {
            final Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        });
        content.addView(appSettings, matchWidth(dp(48)));
        return scroll;
    }

    private void chooseGameFolder() {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_FOLDER);
    }

    private void chooseArchive() {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
            "application/zip", "application/octet-stream", "application/x-iso9660-image"
        });
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_ARCHIVE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some providers grant access only for the current operation.
        }
        setBusy(true, "Importando dados…");
        if (requestCode == REQUEST_GAME_FOLDER) {
            repository.importTreeAsync(uri, this::finishImport);
        } else if (requestCode == REQUEST_GAME_ARCHIVE) {
            repository.importDocumentAsync(uri, this::finishImport);
        }
    }

    private void finishImport(GameDataRepository.ImportResult result) {
        runOnUiThread(() -> {
            importNote.setText(result.message);
            importNote.setTextColor(result.changed ? COLOR_GREEN : COLOR_GOLD);
            setBusy(false, null);
            if (result.changed) {
                Toast.makeText(this, String.format(Locale.ROOT,
                    "%d arquivos importados", result.files), Toast.LENGTH_LONG).show();
            }
            refreshData();
        });
    }

    private void refreshData() {
        setBusy(true, "Verificando dados…");
        repository.scanAsync(result -> runOnUiThread(() -> {
            if (result.canLaunch()) {
                dataState.setText(result.duel10
                    ? "Duelista pronto para inspeção G5b"
                    : "Dados válidos; Duel10 ausente");
                packageState.setText(String.format(Locale.ROOT,
                    "%d/%d pacotes válidos · %d mapas · %s",
                    result.validPackages, EXPECTED_PACKAGES, result.maps, formatBytes(result.bytes)));
            } else {
                dataState.setText("Runtime pronto; dados ausentes");
                packageState.setText("0/107 pacotes · importe sua instalação original");
            }
            launchButton.setText(result.duel10
                ? "Testar duelista · A alterna sala/foco"
                : (result.canLaunch() ? "Abrir runtime nativo" : "Abrir diagnóstico G0"));
            setBusy(false, null);
        }));
    }

    private void setBusy(boolean busy, @Nullable String label) {
        if (progress != null) {
            progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        }
        if (folderButton != null) {
            folderButton.setEnabled(!busy);
        }
        if (archiveButton != null) {
            archiveButton.setEnabled(!busy);
        }
        if (label != null && importNote != null) {
            importNote.setText(label);
            importNote.setTextColor(COLOR_MUTED);
        }
    }

    private LinearLayout gateCard(String gate, String description, String status, int statusColor) {
        final LinearLayout row = card(COLOR_CARD_ALT, 17);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final TextView badge = text(gate, 17, COLOR_TEXT, Typeface.BOLD);
        badge.setGravity(Gravity.CENTER);
        badge.setBackground(roundRect(Color.rgb(52, 59, 77), 16));
        row.addView(badge, new LinearLayout.LayoutParams(dp(58), dp(58)));

        final LinearLayout words = new LinearLayout(this);
        words.setOrientation(LinearLayout.VERTICAL);
        words.setPadding(dp(14), 0, dp(8), 0);
        words.addView(text(description, 15, COLOR_TEXT, Typeface.BOLD));
        row.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        row.addView(text(status, 12, statusColor, Typeface.BOLD));
        return row;
    }

    private TextView sectionTitle(String value) {
        return text(value, 20, COLOR_TEXT, Typeface.BOLD);
    }

    private LinearLayout card(int color, int padding) {
        final LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(padding), dp(padding), dp(padding), dp(padding));
        card.setBackground(roundRect(color, 24));
        return card;
    }

    private TextView text(String value, int sp, int color, int style) {
        final TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setTypeface(Typeface.create("sans", style));
        view.setLineSpacing(0, 1.12f);
        return view;
    }

    private Button primaryButton(String value) {
        final Button button = new Button(this);
        button.setText(value);
        button.setTextSize(17);
        button.setTextColor(Color.rgb(10, 35, 19));
        button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setAllCaps(false);
        button.setBackground(roundRect(COLOR_GREEN, 29));
        return button;
    }

    private Button secondaryButton(String value) {
        final Button button = new Button(this);
        button.setText(value);
        button.setTextSize(15);
        button.setTextColor(COLOR_TEXT);
        button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setAllCaps(false);
        button.setBackground(roundRect(Color.rgb(49, 57, 76), 17));
        return button;
    }

    private Button compactButton(String value) {
        final Button button = secondaryButton(value);
        button.setTextSize(13);
        return button;
    }

    private GradientDrawable roundRect(int color, int radiusDp) {
        final GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private View space(int dp) {
        final View view = new View(this);
        view.setLayoutParams(new LinearLayout.LayoutParams(1, dp(dp)));
        return view;
    }

    private LinearLayout.LayoutParams matchWidth(int height) {
        return new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, height);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static String formatBytes(long bytes) {
        if (bytes >= 1024L * 1024L * 1024L) {
            return String.format(Locale.ROOT, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
        }
        if (bytes >= 1024L * 1024L) {
            return String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0));
        }
        return String.format(Locale.ROOT, "%.1f KB", bytes / 1024.0);
    }
}
