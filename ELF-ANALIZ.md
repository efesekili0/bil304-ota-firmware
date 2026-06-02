# ELF Analizi — new-firmware.z1 (MSP430 ELF)
### BIL304 · Araştırma İş Parçacığı — Toolchain Analizi

> **Analiz ettiğim dosya:** `new-firmware.z1`  
> **Kullandığım araç zinciri:** `msp430-elf-gcc`  
> **Şablon repo:** https://github.com/ismailhakkituran/ota-toolchain-anaysis

---

## 1. Dosya Kimliği — `file` Komutuyla Bakış

İlk olarak ürettiğimiz dosyanın tam olarak ne olduğuna bakmak istedim:

```bash
$ file new-firmware.z1
```

**Aldığım Çıktı:**
```
new-firmware.z1: ELF 32-bit LSB executable, TI msp430, version 1 (embedded), statically linked, with debug_info, not stripped
```

### Buradan ne anlıyoruz?
* **Format:** Dosya 32-bit ELF formatında. 
* **Mimari:** TI MSP430 serisi için derlenmiş (bizim Z1 mote'ların işlemcisi).
* **Endianness:** LSB (Little Endian) kullanılmış, yani düşük anlamlı byte'lar önce geliyor.
* **Bağlantı (Link):** "statically linked" yazıyor, yani dışarıdan bir kütüphaneye bağımlı değil, her şey tek bir dosyanın içine gömülmüş.
* **Debug:** "not stripped" ve "with debug_info" ibareleri, dosyanın içinde debug sembollerinin hala durduğunu gösteriyor. Yani henüz release versiyonu gibi küçültülmemiş.

> **Peki neden düz binary (.bin) kullanmıyoruz da ELF kullanıyoruz?**  
> Düz binary dosyasında sadece makine kodları alt alta durur. Ama ELF formatı, bootloader'a veya işletim sistemine "kod şuradan başlıyor, şu veriyi RAM'e koy, şu veriyi Flash'ta bırak" gibi meta-verileri söylüyor. Geliştirme aşamasında bu bilgilere ihtiyacımız var.

---

## 2. ELF Başlığı (Header) İncelemesi — `readelf -h`

```bash
$ msp430-elf-readelf -h new-firmware.z1
```

**Çıktı:**
```
ELF Header:
  Magic:   7f 45 4c 46 01 01 01 ff 00 00 00 00 00 00 00 00 
  Class:                             ELF32
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            Standalone App
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           Texas Instruments msp430 microcontroller
  Version:                           0x1
  Entry point address:               0x3100
  Start of program headers:          52 (bytes into file)
  Start of section headers:          94584 (bytes into file)
  Flags:                             0x10000001
  Size of this header:               52 (bytes)
  Size of program headers:           32 (bytes)
  Number of program headers:         6
  Size of section headers:           40 (bytes)
  Number of section headers:         21
  Section header string table index: 18
```

### Yorumlarım:
* En baştaki **Magic** değerindeki `7f 45 4c 46`, ASCII olarak `\x7fELF` anlamına geliyor, yani dosyanın imzası.
* **Entry point (Giriş noktası):** `0x3100` olarak görünüyor. Bu çok önemli, cihaz resetlendiğinde doğrudan buradaki startup koduna dallanıyor.
* **OS/ABI:** "Standalone App" diyor. Üzerinde kocaman bir işletim sistemi (Windows/Linux) koşmayan bare-metal sistemler için bu beklediğimiz bir sonuç.
* **21 bölüm başlığı:** Kod, veri, debug ve kesme (interrupt) bölümlerini içerir.

---

## 3. Bölümler (Sections) — `readelf -S`

Derleyicinin kodumuzu ve değişkenlerimizi nasıl parçaladığını görmek için section tablosuna baktım:

```bash
$ msp430-elf-readelf -S new-firmware.z1
```

**En Önemli Bölümlerin Özeti:**
```
Section Headers:
  [Nr] Name              Type            Addr     Off    Size   Flg
  [ 1] .far.text         PROGBITS        00010000 00cff2 004a78  AX
  [ 2] .text             PROGBITS        00003100 0000f4 00976e  AX
  [ 3] .rodata           PROGBITS        0000c870 009864 0035fd   A
  [ 4] .data             PROGBITS        00001100 00ce62 000150  WA
  [ 5] .bss              NOBITS          00001250 00cfb2 001648  WA
  [ 7] .vectors          PROGBITS        0000ffc0 00cfb2 000040  AX
```

### Neyi Nereye Koymuş?

* **`.text` (Çalışma Adresi: 0x3100 | Boyut: 37.8 KB):**  
  Yazdığımız asıl kodların makine diline çevrilmiş hali burada tutuluyor. `AX` (Allocate ve Execute) bayrakları sayesinde bunun çalıştırılabilir bir kod olduğunu ve Flash belleğe yerleştiğini anlıyoruz.

* **`.far.text` (Çalışma Adresi: 0x10000 | Boyut: 18.6 KB):**  
  MSP430X mimarisinin 20-bit adres uzayına erişen kod bölgesi. Klasik 64KB adres sınırının üzerindeki (`0x10000`) kısımlar.

* **`.rodata` (Çalışma Adresi: 0xC870 | Boyut: 13.5 KB):**  
  Printf içindeki metinler, sabit (const) değerler vs. burada. Read-Only (A bayrağı) olduğu için RAM'e gitmesine gerek yok, Flash'ta kalıyor.

* **`.data` (Çalışma Adresi: 0x1100 | Boyut: 336 B):**  
  Kodda başlangıç değeri verdiğimiz global değişkenler burada. Normalde Flash'ta saklanıyor ama program başlarken RAM'e (0x1100 adresine) kopyalanıyor (`WA` - Write/Allocate).

* **`.bss` (Çalışma Adresi: 0x1250 | Boyut: 5.7 KB):**  
  Başlangıç değeri atanmamış global değişkenlerimiz. İlginç tarafı Type kısmında `NOBITS` yazması; yani bu değişkenler aslında ELF dosyasında (ve Flash'ta) hiç yer kaplamıyor. Sadece "RAM'de 5.7 KB'lık yer ayır ve hepsini sıfırla" talimatı veriyor.

* **`.vectors` (Adres: 0xFFC0 - 0xFFFF):**  
  Z1 Mote'un (MSP430) 32 adet kesme vektörünün adresi burada listelenir.

---

## 4. Segment Yerleşimi (Program Headers) — `readelf -l`

Bu komutla işletim sisteminin/bootloader'ın dosyayı belleğe nasıl yükleyeceğini (mapping) kontrol ettim:

```bash
$ msp430-elf-readelf -l new-firmware.z1
```

**Çıktının Kritik Kısmı:**
```
Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x000000 0x0000300c 0x0000300c 0x09862 0x09862 R E 0x1
  LOAD           0x00ce62 0x00001100 0x0000fe6e 0x00150 0x01798 RW  0x1
```

Burada **VMA** (Virtual Address) ile **LMA** (Physical Address) farkını çok net görebiliyoruz:
* `.data` segmenti için LMA (`0xfe6e`) ile VMA (`0x1100`) farklı. Yani bu veriler Flash'ta 0xfe6e'de duruyor ama çalışma zamanında 0x1100'e (RAM'e) kopyalanıyor.
* `.text` segmenti için iki adres de aynı. Kodları RAM'e kopyalamaya gerek yok, doğrudan Flash üzerinden çalıştırılabiliyor (Execute In Place).

---

## 5. Boyutların Hesaplanması — `msp430-size`

Yazdığımız uygulamanın donanımı ne kadar zorladığına bakmak istedim:

```bash
$ msp430-size new-firmware.z1
```

**Çıktı:**
```
   text     data      bss      dec      hex    filename
  71715      336     5706    77757    12fbd    new-firmware.z1
```

### Hesaplamalarım:
* **Flash (ROM) Harcaması:** `text` + `data` = 71,715 + 336 = **70.4 KB**
* **RAM (SRAM) Harcaması:** `data` + `bss` = 336 + 5,706 = **5.9 KB**

**Peki bu Z1 mote için ne ifade ediyor?**
Z1 mote'ların işlemcisi olan MSP430F2617'de 92 KB Flash ve 8 KB RAM var. 
Hesapladığımda Flash'ın **%76**'sını, RAM'in ise **%74**'ünü doldurmuş durumdayız. RAM sınırına epey yaklaşmışız; bunun sebebi kullandığımız IPv6 ve RPL/UDP ağ yığınlarının arkada çok fazla bellek tamponu (buffer) kullanıyor olması.

---

## 6. Sembol Tablosuna Bakış — `nm` Komutu

Uygulamadaki fonksiyonların ve değişkenlerin bellek adreslerini listelemek için nm komutunu kullandım:

```bash
$ msp430-elf-nm -n new-firmware.z1 | grep -E "main|__br_unexpected_|autostart_start"
```

Çok uzun bir liste çıktı ama en çok işime yarayanlar şunlardı:
```
0000313e T main
00003376 t __br_unexpected_
00003a3a T autostart_start
```

Buradaki `T` harfi, bu sembollerin `.text` bölümünde yer alan genel (public) fonksiyonlar olduğunu belirtiyor. Mesela bizim C kodunda yazdığımız `main` fonksiyonu, derleme bitince bellekte tam olarak `0x313e` adresine denk gelmiş. Entry point (`0x3100`) ile main (`0x313e`) arasındaki o boşlukta startup kodları çalışıyor.

---

## 7. String ve Metadata Analizi — `strings` Komutu

Bir de derlenmiş dosyanın içinde açık metin (string) olarak neler kalmış diye bakmak istedim. Belki loglar veya IP adresleri yakalarım diye `strings` komutunu çalıştırdım:

```bash
$ msp430-elf-strings new-firmware.z1 | grep -i "IPv6\|Contiki"
```

**Bulduğum bazı çıktılar:**
```
Starting Contiki-NG-release/v4.8-625-g8518cbaff-dirty
Tentative link-local IPv6 address: 
IPv6 addresses:
IPv6 cache full, dropping DIO
output: sending IPv6 packet with len %d
```

**Bunlar ne anlama geliyor?**
1. Kodun Contiki-NG'nin v4.8 sürümüyle derlendiği stringlerin içinde kabak gibi kalmış.
2. Arka planda IPv6 paketlerinin atıldığına dair loglar var.
3. RPL protokolü açıkça çalışıyor, çünkü `DIO` paketlerinin (DODAG Information Object) droplandığını gösteren bir debug mesajı kalmış.

---

## 8. Assembly ve Makine Kodu Analizi — `objdump` Komutu

C ile yazdığımız kod işlemcide (assembly olarak) neye dönüşüyor diye merak edip `objdump` komutunu denedim. Sadece `main` fonksiyonunun başındaki ilk birkaç satırı aldım:

```bash
$ msp430-elf-objdump -d new-firmware.z1 | grep -A 10 "<main>:"
```

**Çıktı:**
```assembly
0000313e <main>:
    313e:	b0 13 e4 6a 	calla	#27364		;0x06ae4 (platform_init_stage_one)
    3142:	b0 13 3a 45 	calla	#17722		;0x0453a (clock_init)
    3146:	b0 13 da aa 	calla	#43738		;0x0aada (rtimer_init)
    314a:	b0 13 7c 6d 	calla	#28028		;0x06d7c (process_init)
    314e:	0e 43       	clr	r14
```

**Dikkatimi çekenler:**
- İşlemci MSP430X mimarisi olduğu için normal `call` komutu yerine daha geniş adresleme yapabilen `calla` (Call Anywhere) kullanılmış.
- C kodunda `main()` içine girdiğimiz an işletim sistemi arka planda cihazı hazırlamak için sırasıyla `platform_init`, `clock_init` falan çağırıyor. (Assembly yanındaki parantez içi yorumları sembol adreslerine bakarak ben eşleştirdim).
- Alttaki `clr r14` komutunda derleyicinin R14 register'ının içini temizlediğini görüyoruz, ufak bir optimizasyon temizliği yapılmış.

---

## 9. CC1352R Donanımıyla Karşılaştırma

Aynı derlenmiş dosyayı (`new-firmware.z1`) modern bir ARM cihaz olan **CC1352R**'ye atsaydım çalışır mıydı diye düşündüm. Tabi ki çalışmazdı.

Çünkü bellek haritaları tamamen alakasız:
* Z1'de Flash bellek `0x3100` adresinden başlıyor (analizde kodların oraya yerleştiğini de gördük).
* CC1352R'de ise ana Flash `0x00000000` adresinden başlıyor.
* Üstelik Z1 16-bit bir işlemciyken, CC1352R 32-bit ARM Cortex-M4F. Makine komutları bile birbirini tutmuyor. Yani CC1352R'de çalıştırmak istersek kodu ARM derleyicisiyle (`arm-none-eabi-gcc`) baştan derlemek şart.

---

## Kısaca Özetlersek

Sadece kod yazıp bırakmak yerine ELF dosyasının içine bakmak, arka planda derleyicinin RAM'i ve Flash'ı nasıl böldüğünü (bss, data, text mantığı) anlamamı sağladı. Gömülü sistemlerde sadece C bilmenin yetmediğini, aynı zamanda derleyici araçlarını ve bellek haritasını okumayı da bilmek gerektiğini tecrübe etmiş oldum.
