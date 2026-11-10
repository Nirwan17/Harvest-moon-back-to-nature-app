package id.penyu.modmenu;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

/**
 * Mod menu Harvest Moon BTN.
 * Tombol "MOD" melayang (bisa digeser) -> panel dengan topik dropdown.
 * Seluruh logika cheat ada di libmodmenu.so (modmenu.cpp).
 */
public final class ModMenu {
    private static final String TAG = "HMBTNMod";

    // ---- JNI (diimplementasikan di modmenu.cpp)
    private static native void nInstall();
    private static native int nCatCount();
    private static native String nCatName(int c);
    private static native int nCatSize(int c);
    private static native int nCatGroup(int c, int i);
    private static native String nGroupName(int g);
    private static native boolean nGroupExp(int g);
    private static native boolean nIsOn(int g);
    private static native void nSetOn(int g, boolean on);
    private static native void nClearAll();
    private static native int nActiveCount();
    private static native String nStatus();
    private static native String nScan(int value, boolean first);
    private static native String nResetOff();

    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static boolean nativeOk = false;
    private static boolean started = false;
    private static Activity act;
    private static Dialog dialog;
    private static TextView statusView;
    private static final List<Cat> cats = new ArrayList<Cat>();

    private static final class Cat {
        String name;
        int index;            // indeks kategori di native, -1 = alat
        int size;
        TextView head;
        LinearLayout body;
        boolean built;
        boolean open;
        final List<CheckBox> boxes = new ArrayList<CheckBox>();
    }

    // ================================================================ dipanggil dari HmActivity.onCreate
    /** Sebelum super.onCreate: muat library & pasang hook sebelum game mulai. */
    public static void preload(Activity a) {
        try {
            System.loadLibrary("hmbtn");
            System.loadLibrary("modmenu");
            nInstall();
            nativeOk = true;
        } catch (Throwable t) {
            Log.e(TAG, "preload gagal", t);
        }
    }

    /** Sesudah super.onCreate: tampilkan tombol melayang. */
    public static void start(Activity a) {
        if (!nativeOk || started) return;
        started = true;
        act = a;
        ui.postDelayed(new Runnable() {
            int tries = 0;
            @Override public void run() {
                try {
                    View decor = act.getWindow().getDecorView();
                    if (decor.getWindowToken() == null && tries < 60) {
                        tries++;
                        ui.postDelayed(this, 500);
                        return;
                    }
                    addFloatingButton();
                } catch (Throwable t) {
                    Log.e(TAG, "gagal menambah tombol", t);
                    if (tries++ < 60) ui.postDelayed(this, 1000);
                }
            }
        }, 1500);
    }

    // ================================================================ util UI
    private static int dp(float v) {
        return (int) (v * act.getResources().getDisplayMetrics().density + 0.5f);
    }

    private static GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radiusDp));
        return g;
    }

    private static TextView tv(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(act);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private static void toast(String s) {
        try { Toast.makeText(act, s, Toast.LENGTH_LONG).show(); } catch (Throwable ignored) { }
    }

    // ================================================================ tombol melayang
    private static void addFloatingButton() {
        final WindowManager wm = act.getWindowManager();
        final TextView btn = tv("MOD", 13, Color.WHITE, true);
        btn.setGravity(Gravity.CENTER);
        btn.setPadding(dp(12), dp(8), dp(12), dp(8));
        btn.setBackground(rounded(0xCC2E7D32, 18));
        btn.setAlpha(0.8f);

        final WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.x = dp(8);
        lp.y = dp(56);

        btn.setOnTouchListener(new View.OnTouchListener() {
            float downX, downY;
            int startX, startY;
            boolean moved;
            @Override public boolean onTouch(View v, MotionEvent e) {
                switch (e.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        downX = e.getRawX(); downY = e.getRawY();
                        startX = lp.x; startY = lp.y; moved = false;
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        float mx = e.getRawX() - downX, my = e.getRawY() - downY;
                        if (Math.abs(mx) > dp(6) || Math.abs(my) > dp(6)) moved = true;
                        if (moved) {
                            lp.x = startX + (int) mx;
                            lp.y = startY + (int) my;
                            try { wm.updateViewLayout(btn, lp); } catch (Throwable ignored) { }
                        }
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                        if (!moved) showPanel();
                        return true;
                    default:
                        return false;
                }
            }
        });
        wm.addView(btn, lp);
        Log.i(TAG, "tombol MOD ditambahkan");
    }

    // ================================================================ panel
    private static void showPanel() {
        try {
            if (dialog != null && dialog.isShowing()) return;
            buildPanel();
        } catch (Throwable t) {
            Log.e(TAG, "gagal membuka panel", t);
            toast("Mod menu error: " + t);
        }
    }

    private static void buildPanel() {
        cats.clear();
        final Activity a = act;

        LinearLayout root = new LinearLayout(a);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackground(rounded(0xF01E1E26, 14));
        root.setPadding(dp(10), dp(8), dp(10), dp(8));

        // --- header
        LinearLayout head = new LinearLayout(a);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = tv("HM BTN Mod Menu", 16, Color.WHITE, true);
        head.addView(title, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        TextView close = tv("  X  ", 18, 0xFFFF8A80, true);
        close.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { if (dialog != null) dialog.dismiss(); }
        });
        head.addView(close);
        root.addView(head);

        statusView = tv(nStatus(), 11, 0xFFB0BEC5, false);
        statusView.setPadding(0, dp(2), 0, dp(2));
        root.addView(statusView);
        root.addView(tv("Oranye = eksperimental | [?] = alamat belum pasti | ketuk topik untuk membuka",
                10, 0xFF78909C, false));

        // --- daftar topik (dropdown)
        ScrollView sv = new ScrollView(a);
        LinearLayout list = new LinearLayout(a);
        list.setOrientation(LinearLayout.VERTICAL);
        sv.addView(list);
        LinearLayout.LayoutParams svlp =
                new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        svlp.topMargin = dp(6);
        root.addView(sv, svlp);

        int n = nCatCount();
        for (int c = 0; c < n; c++) addCategory(list, c);
        addToolsCategory(list);

        // --- footer
        Button off = new Button(a);
        off.setText("Matikan semua cheat");
        off.setAllCaps(false);
        off.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { nClearAll(); refreshAll(); }
        });
        root.addView(off);

        dialog = new Dialog(a, android.R.style.Theme_DeviceDefault_Dialog);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setContentView(root);
        dialog.show();

        Window w = dialog.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            w.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            DisplayMetrics dm = a.getResources().getDisplayMetrics();
            WindowManager.LayoutParams wl = new WindowManager.LayoutParams();
            wl.copyFrom(w.getAttributes());
            wl.width = Math.max(dp(320), Math.min((int) (dm.widthPixels * 0.62f), dp(540)));
            wl.height = (int) (dm.heightPixels * 0.92f);
            wl.dimAmount = 0.25f;
            w.setAttributes(wl);
        }

        ui.postDelayed(new Runnable() {
            @Override public void run() {
                if (dialog != null && dialog.isShowing() && statusView != null) {
                    statusView.setText(nStatus());
                    ui.postDelayed(this, 1000);
                }
            }
        }, 1000);
    }

    private static void addCategory(LinearLayout list, int c) {
        final Cat cat = new Cat();
        cat.index = c;
        cat.name = nCatName(c);
        cat.size = nCatSize(c);
        buildCatViews(list, cat);
        cats.add(cat);
    }

    private static void buildCatViews(LinearLayout list, final Cat cat) {
        cat.head = tv("", 14, Color.WHITE, true);
        cat.head.setPadding(dp(10), dp(10), dp(10), dp(10));
        cat.head.setBackground(rounded(0xFF33333F, 8));
        LinearLayout.LayoutParams hl = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        hl.topMargin = dp(5);
        list.addView(cat.head, hl);

        cat.body = new LinearLayout(act);
        cat.body.setOrientation(LinearLayout.VERTICAL);
        cat.body.setPadding(dp(8), dp(2), dp(4), dp(4));
        cat.body.setVisibility(View.GONE);
        list.addView(cat.body);

        cat.head.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { toggleCategory(cat); }
        });
        updateHead(cat);
    }

    private static void toggleCategory(Cat cat) {
        boolean willOpen = !cat.open;
        for (Cat o : cats) {                       // akordeon: tutup yang lain
            if (o != cat && o.open) { o.open = false; o.body.setVisibility(View.GONE); updateHead(o); }
        }
        cat.open = willOpen;
        if (willOpen && !cat.built) {
            cat.built = true;
            if (cat.index >= 0) buildRows(cat); else buildTools(cat);
        }
        cat.body.setVisibility(willOpen ? View.VISIBLE : View.GONE);
        updateHead(cat);
    }

    private static void buildRows(final Cat cat) {
        for (int i = 0; i < cat.size; i++) {
            final int g = nCatGroup(cat.index, i);
            if (g < 0) continue;
            CheckBox cb = new CheckBox(act);
            cb.setText(nGroupName(g));
            cb.setTextSize(13);
            cb.setTextColor(nGroupExp(g) ? 0xFFFFB74D : 0xFFECEFF1);
            cb.setChecked(nIsOn(g));
            cb.setTag(Integer.valueOf(g));
            cb.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    nSetOn(g, ((CheckBox) v).isChecked());
                    refreshAll();
                }
            });
            cat.boxes.add(cb);
            cat.body.addView(cb);
        }
    }

    private static void updateHead(Cat cat) {
        String arrow = cat.open ? "\u25BE " : "\u25B8 ";
        if (cat.index < 0) { cat.head.setText(arrow + cat.name); return; }
        int active = 0;
        for (int i = 0; i < cat.size; i++) {
            int g = nCatGroup(cat.index, i);
            if (g >= 0 && nIsOn(g)) active++;
        }
        cat.head.setText(arrow + cat.name + "  (" + active + "/" + cat.size + ")");
        cat.head.setTextColor(active > 0 ? 0xFF81C784 : Color.WHITE);
    }

    private static void refreshAll() {
        for (Cat c : cats) {
            for (CheckBox cb : c.boxes) cb.setChecked(nIsOn(((Integer) cb.getTag()).intValue()));
            updateHead(c);
        }
        if (statusView != null) statusView.setText(nStatus());
    }

    // ================================================================ alat & kalibrasi
    private static void addToolsCategory(LinearLayout list) {
        Cat cat = new Cat();
        cat.index = -1;
        cat.name = "Alat & Kalibrasi";
        buildCatViews(list, cat);
        cats.add(cat);
    }

    private static void buildTools(Cat cat) {
        LinearLayout b = cat.body;
        TextView info = tv("Jika cheat tidak berefek tapi status di atas menunjukkan memori OK, "
                + "alamat RAM mungkin bergeser. Isi jumlah uangmu SEKARANG (angka unik, mis. 12345), "
                + "lalu tekan Cari. Jika kandidat banyak: ubah uangmu (jual/beli), isi angka baru, "
                + "tekan Saring.", 11, 0xFFB0BEC5, false);
        b.addView(info);

        final EditText input = new EditText(act);
        input.setHint("Jumlah uang saat ini");
        input.setHintTextColor(0xFF78909C);
        input.setTextColor(Color.WHITE);
        input.setInputType(InputType.TYPE_CLASS_NUMBER);
        b.addView(input);

        final TextView result = tv("", 12, 0xFFFFF59D, false);

        LinearLayout row = new LinearLayout(act);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button find = new Button(act);
        find.setText("Cari"); find.setAllCaps(false);
        Button filter = new Button(act);
        filter.setText("Saring"); filter.setAllCaps(false);
        Button reset = new Button(act);
        reset.setText("Reset"); reset.setAllCaps(false);
        LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        row.addView(find, bl); row.addView(filter, bl); row.addView(reset, bl);
        b.addView(row);
        b.addView(result);

        find.setOnClickListener(scanListener(input, result, true));
        filter.setOnClickListener(scanListener(input, result, false));
        reset.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                result.setText(nResetOff());
                if (statusView != null) statusView.setText(nStatus());
            }
        });
    }

    private static View.OnClickListener scanListener(final EditText input, final TextView result, final boolean first) {
        return new View.OnClickListener() {
            @Override public void onClick(View v) {
                try {
                    int value = Integer.parseInt(input.getText().toString().trim());
                    if (value <= 0) { result.setText("Isi angka lebih dari 0."); return; }
                    result.setText(nScan(value, first));
                } catch (NumberFormatException e) {
                    result.setText("Angka tidak valid.");
                } catch (Throwable t) {
                    result.setText("Error: " + t);
                }
                if (statusView != null) statusView.setText(nStatus());
            }
        };
    }
}
