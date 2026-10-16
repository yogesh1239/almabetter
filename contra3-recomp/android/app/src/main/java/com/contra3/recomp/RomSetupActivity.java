package com.contra3.recomp;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.util.zip.CRC32;

/**
 * First-run screen: the player picks their own Contra III (USA) ROM with the
 * system file picker. It is checked (size, CRC32) and copied into the app's
 * private files directory; afterwards the game starts directly.
 */
public class RomSetupActivity extends Activity {
    static final String ROM_NAME = "Contra III - The Alien Wars (USA).sfc";
    static final int ROM_SIZE = 1048576;
    static final long ROM_CRC32 = 0x84da7cfeL;
    private static final int PICK_ROM = 1;

    private TextView mStatus;

    static File romFile(Activity a) {
        return new File(a.getExternalFilesDir(null), ROM_NAME);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        File rom = romFile(this);
        if (rom.isFile() && rom.length() == ROM_SIZE) {
            startGame();
            return;
        }

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Contra III: The Alien Wars — Recompiled");
        title.setTextColor(Color.WHITE);
        title.setTextSize(22);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        mStatus = new TextView(this);
        mStatus.setText("Select your own Contra III (USA) ROM (.sfc or .smc).\n"
                + "No game data is included with this app.");
        mStatus.setTextColor(Color.LTGRAY);
        mStatus.setTextSize(16);
        mStatus.setGravity(Gravity.CENTER);
        mStatus.setPadding(0, pad, 0, pad);
        layout.addView(mStatus);

        Button pick = new Button(this);
        pick.setText("Select ROM");
        pick.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_ROM);
        });
        layout.addView(pick);
        setContentView(layout);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null)
            return;
        String error = importRom(data.getData());
        if (error != null) {
            mStatus.setText(error);
            return;
        }
        startGame();
    }

    private String importRom(Uri uri) {
        byte[] bytes;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream(ROM_SIZE + 512);
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                if (out.size() > 4 * ROM_SIZE)
                    return "That file is too large to be the Contra III ROM.";
            }
            bytes = out.toByteArray();
        } catch (Exception e) {
            return "Could not read that file: " + e.getMessage();
        }
        int offset = bytes.length % 1024 == 512 ? 512 : 0; /* copier header */
        if (bytes.length - offset != ROM_SIZE)
            return "That file is not the Contra III (USA) ROM (wrong size).";
        CRC32 crc = new CRC32();
        crc.update(bytes, offset, ROM_SIZE);
        if (crc.getValue() != ROM_CRC32)
            return String.format("That ROM does not match Contra III (USA)\n"
                    + "(CRC32 %08x, expected %08x).", crc.getValue(), ROM_CRC32);
        try (FileOutputStream out = new FileOutputStream(romFile(this))) {
            out.write(bytes, offset, ROM_SIZE);
        } catch (Exception e) {
            return "Could not save the ROM: " + e.getMessage();
        }
        return null;
    }

    private void startGame() {
        startActivity(new Intent(this, Contra3Activity.class));
        finish();
    }
}
