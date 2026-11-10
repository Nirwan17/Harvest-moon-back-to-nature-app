# HM BTN Mod Menu

Menu cheat berkategori (dropdown) untuk Harvest Moon BTN buatan Penyu (v1.3.1, arm64).
157 cheat dari `cheats.txt` (format GameShark SLUS-011.15) -> 19 topik.

## 1. Build di GitHub (cukup dari HP)
1. Buat repo baru, lalu Add file > Upload files: unggah SEMUA file di folder ini
   (cheats.txt, gen_cheats.py, modmenu.cpp, ModMenu.java, PATCH_SMALI.txt, build.yml, README.md).
2. Buka `build.yml` di GitHub > ikon pensil (Edit) > ubah nama file di atas menjadi
   `.github/workflows/build.yml` > Commit.
3. Tab Actions > "Build Mod Menu" (jalan otomatis, atau Run workflow). Tunggu centang hijau.
4. Buka run-nya > bagian Artifacts > unduh `modmenu-hasil` > ekstrak.
   Isinya: lib/arm64-v8a/libmodmenu.so, classes2.dex, PATCH_SMALI.txt.

## 2. Pasang ke APK (MT Manager)
1. BACKUP folder /storage/emulated/0/HarvestMoonBTN dan APK aslinya.
2. Buka APK Penyu > Lihat:
   - tambahkan `libmodmenu.so` ke folder lib/arm64-v8a/
   - tambahkan `classes2.dex` ke root APK
3. Buka classes.dex (Dex Editor++) > id.penyu.hmbtn.HmActivity > edit smali >
   tempel method dari PATCH_SMALI.txt di akhir file > simpan.
4. Simpan APK + tanda tangani. Uninstall versi lama, pasang APK baru.

## 3. Pemakaian
- Tombol hijau "MOD" melayang (bisa digeser). Ketuk -> panel.
- Ketuk judul topik untuk membuka/menutup (satu topik terbuka sekali), centang cheat.
- Baris status di atas panel harus menampilkan Uang/Stamina yang SAMA dengan di game.
  Kalau beda -> topik "Alat & Kalibrasi" (isi jumlah uangmu, Cari).
- Cheat yang menulis alamat sama otomatis saling mematikan (mis. Hujan vs Pagi).
- Oranye = eksperimental (patch kode, kemungkinan besar tidak jalan di emulator ini).
  [?] = alamat di luar blok data utama, belum pasti.
- Cheat besar (Modif Kebun, Tanam Rumput, Pagar, Tanaman Custom) menimpa kebun selama aktif.
  Matikan setelah selesai. Mematikan cheat TIDAK mengembalikan nilai lama.
- Log: /storage/emulated/0/HarvestMoonBTN/modmenu.log (kirim isinya kalau ada masalah).

## Edit cheat
Ubah `cheats.txt` (baris "#judul" lalu kode 80/30/D0), commit, build ulang.
Banner "#=====NAMA=====" membuat topik baru.
