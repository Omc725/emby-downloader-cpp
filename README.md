# emby-downloader-cpp

Emby medya sunuculari icin C++17 ve libcurl ile gelistirilmis yuksek performansli terminal indirme araci.

## Gereksinimler

- C++17 destekli derleyici (GCC >= 9 veya Clang >= 10)
- libcurl gelistirme basliklari (`libcurl4-openssl-dev`)
- make veya cmake
- FFmpeg (yalnizca donusturmesiz tekil ses akisi kopyalama icin)

Debian/Ubuntu uzerinde paket kurulumu:

```bash
sudo apt update
sudo apt install g++ make libcurl4-openssl-dev ffmpeg
```

## Derleme

Makefile kullanarak derleme:

```bash
make
```

veya CMake ile derleme:

```bash
cmake -B build
cmake --build build
```

Derleme sonucunda calistirilabilir `emby-downloader` ikili dosyasi olusturulur.

## Kullanim

```bash
./emby-downloader
```

## Ozellikler

- **Donusturmesiz Dogrudan Video Indirme (Bit-for-bit):** Video akisina transcode uygulanmaksizin Emby sunucusunun ham disk icerigi dogrudan indirilir. Sunucu disk boyutu ile yerel dosya boyutu bayt duzeyinde karsilastirilarak dogrulanir.
- **HTTP Range Destegi:** Ag kesintilerinde indirme islemi bastan baslamaz, kaldigi bayttan devam eder.
- **Donusturmesiz Ses Cikarma:** FFmpeg `-c:a copy` parametresi ile video indirilmeden dogrudan ham ses kanali diske kaydedilir.
- **Altyazi Indirme:** Dahili ve harici altyazilar metin formatinda kaydedilir.
- **Indirme Kuyrugu:** Gorevleri kuyruga ekleme, listeleme, silme ve ardisik yurutme. Oturumlar arasinda kuyruk `~/.config/emby-downloader/queue.json` dosyasinda saklanir.
- **Dizi Toplu Indirme:** Sezon bazinda tum bolumleri veya bolum araligini toplu indirme.
- **Coklu Profil Yonetimi:** Sunucu baglanti profilleri `~/.config/emby-downloader/config.json` dosyasinda saklanir.
- **Hiyerarsik Gezinme:** Her asamada yalnizca bir adim geriye donus (`[0] Geri`).
- **Canli Konsol Gostergesi:** Yuzde, indirme hizi (MB/s ve Mbps), indirilen/toplam bayt boyutu ve tahmini kalan sure (ETA).
