# BIL304 — OTA Firmware Güncelleme Sistemi
### Ondokuz Mayıs Üniversitesi · Bilgisayar Mühendisliği · İşletim Sistemleri

> **Ders:** BIL 304 – İşletim Sistemleri  
> **Platform:** Contiki-NG · Cooja Simülatörü · Z1 Mote (MSP430F2617)  
> **Konu:** Over-The-Air (OTA) Firmware Aktarımı — Stop-and-Wait RDT + Checksum Bütünlük Doğrulaması

---

## 🎥 Demo Videosu

> **YouTube:** `[VIDEO LİNKİ BURAYA EKLENECEKTİR]`

Videoda Cooja simülasyonunu, Stop-and-Wait veri aktarımını, checksum denetimimizi ve yazdığımız algoritmaların terminaldeki çıktılarını detaylıca anlattık.

---

## 📁 Proje Yapısı

```
twota/
├── contiki-ng/
│   └── examples/
│       └── ota-firmware/
│           ├── udp-client.c       (Gönderici / Node 2)
│           ├── udp-server.c       (Alıcı / Node 1)
│           ├── Makefile
│           ├── project-conf.h
│           ├── new-firmware.z1    (İletilecek asıl dosya - 129760 byte)
│           └── BIL304-OS-Project-1.csc   (Cooja senaryo dosyası)
└── README.md
```

---

## 1. Düğüm Rolleri ve Sistem Akışı

Projeyi 3 düğüm (node) üzerinden kurguladık:
- **Node 1:** Alıcı cihaz (RPL ağının kökü, UDP Server)
- **Node 2:** Gönderici cihaz (UDP Client, yazılımı yollayan)
- **Node 3:** Sadece paketleri ileten yönlendirici (RPL Forwarder)

Gönderici ve yönlendirici düğümlere aynı kodu (`udp-client.c`) yükledik. Ancak Node 3'ün de dosya göndermeye kalkmasını engellemek için kodun başına bir ID kontrolü koyduk:

```c
if(node_id != 2) {
    printf("OTA: Ben gonderici degilim (Node ID: %u). Yonlendirici modunda.\n", node_id);
    PROCESS_EXIT();
}
```

Bu sayede Node 3 sadece ağda köprü görevi görüyor.

---

## 2. Paket Yapısı ve Blok Mantığı

IEEE 802.15.4 radyo katmanının veri sınırlarını aşmamak için 129 KB'lık firmware dosyasını 64 byte'lık küçük bloklara bölerek yolladık. Her bloğun başına bizim yazdığımız 5 byte'lık bir OTA başlığı ekleniyor.

Paket başlığımız şu şekilde:
```c
typedef struct {
    uint16_t block_id;
    uint8_t  data_len;
    uint16_t checksum;
} __attribute__((packed)) ota_header_t;
```

**Matematiksel Boyutlar:**
- Firmware boyutu: 129760 Byte (new-firmware.z1)
- Blok boyutu: 64 Byte veri + 5 Byte başlık = 69 Byte toplam
- Toplam gönderilecek blok: 2028 adet (129760 / 64)
- En sondaki EOF (Bitiş) paketi veri taşımaz, sadece başlık gider.

---

## 3. Aktarım Protokolü (Stop-and-Wait)

IoT cihazlarında (Z1 Mote) çok az RAM olduğu için aynı anda onlarca paketi havada tutamayız. Bu yüzden aktarımı **Stop-and-Wait (Dur-ve-Bekle)** mantığıyla yazdık.

Gönderici bir bloğu yollar ve sayacı başlatır (biz 2 saniye verdik). Alıcıdan onay (ACK) gelmeden asla yeni bloğa geçmez. Eğer ACK yolda kaybolursa, süresi dolan gönderici aynı bloğu tekrar yollar.

```c
while(!transfer_done) {
    if(!waiting_for_ack && bytes_sent < FIRMWARE_SIZE) {
        send_ota_block();
    }

    PROCESS_WAIT_EVENT();

    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&retransmit_timer)) {
        if(waiting_for_ack && bytes_sent < FIRMWARE_SIZE) {
            global_cksum = cksum_before_block;
            waiting_for_ack = false;
            send_ota_block();
        }
    }
}
```

Alıcı tarafı da sadece beklediği sıra numarası (`expected_block`) gelirse veriyi kabul ediyor. Eğer zaten aldığı bir blok tekrar gelirse (gönderici ACK alamayıp tekrar yolladıysa), alıcı veriyi ikinci kez yazmıyor ama göndericinin ilerlemesi için ACK'yı tekrar yolluyor.

```c
if(hdr.block_id == expected_block) {
    expected_block++;
    ack = hdr.block_id;
    simple_udp_sendto(&udp_conn, &ack, sizeof(ack), sender_addr);

} else if(hdr.block_id < expected_block) {
    ack = hdr.block_id;
    simple_udp_sendto(&udp_conn, &ack, sizeof(ack), sender_addr);
}
```

---

## 4. Hata Tespiti (Checksum Algoritması)

Havadaki radyo gürültüsünden dolayı verinin bozulma ihtimaline karşı bir sağlama (checksum) algoritması ekledik.

Normalde MD5 veya CRC32 çok daha güçlüdür ama MSP430 işlemcisini inanılmaz yorar ve pili tüketir. Bu yüzden biz CPU'yu en az yoran **Aritmetik Byte-Toplam** yöntemini tercih ettik. Fonksiyon sadece byte'ları topluyor ve 16-bit taşmasını otomatik avantaja çeviriyor:

```c
static uint16_t block_checksum(const uint8_t *data, uint8_t len) {
    uint16_t s = 0;
    uint8_t  i;
    for(i = 0; i < len; i++) {
        s += data[i];
    }
    return s;
}
```

Alıcı bu toplam değerini, gelen veri üzerinden kendi hesapladığı değerle karşılaştırıyor. Eğer uyuşmazlık varsa paketi reddedip onay yollamıyor, böylece gönderici bir süre sonra paketi tekrar iletiyor.

```c
calc_cksum = block_checksum(payload, hdr.data_len);
if(calc_cksum != hdr.checksum) {
    printf("OTA RX: [HATA] Blok %u checksum hatasi! beklenen=0x%04x hesaplanan=0x%04x\n",
           hdr.block_id, hdr.checksum, calc_cksum);
    return;
}
```

**Global Doğrulama:** 2028 blok bittiğinde, gönderici o ana kadar gönderdiği tüm baytların genel bir toplamını EOF (Bitiş) paketiyle yollar. Alıcı da kendi genel toplamına bakar. İkisi eşleşirse dosya 129 KB boyunca sıfır hatayla ulaşmış demektir.

```c
if(calculated_global_cksum == hdr.checksum) {
    printf("OTA RX: [OK] Global checksum dogrulandi: 0x%04x\n", calculated_global_cksum);
    printf("Yuklenmeye hazir yeni firmware alimi tamamlandi.\n");
} else {
    printf("OTA RX: [HATA] Global checksum uyusmazligi!\n");
}
```

---

## 5. Önemli Tasarım Kararları

### 5.1 Neden Dosya Sistemi (CFS) Kullanmadık?
Contiki'nin Coffee File System (CFS) kütüphanesi çok fazla bellek kaplıyor (yaklaşık 45KB). Z1 Mote'un 52KB olan ROM'una bizim yazdığımız ağ kodlarıyla beraber bunu sığdırmamız imkansızdı. Şartnamedeki izne dayanarak, blokların gelişini sayaçla ve global checksum ile bellekte sanal olarak doğrulama yöntemini seçtik. 

### 5.2 Deterministic Firmware Üretimi
Cooja simülasyonunda 129KB veriyi direkt Z1'in hafızasına gömemeyeceğimiz için iki tarafta da aynı mantıkla çalışan matematiksel bir formül yazdık:
`buf[i] = (uint8_t)((offset + i) % 256u);`
Böylece verinin aktarımı tamamen simüle edilmiş oldu.

---

## 6. Projeyi Derleme ve Çalıştırma

```bash
cd contiki-ng/examples/ota-firmware
make TARGET=z1 clean
make TARGET=z1 -j$(nproc)
```

Çıkan binary boyutları (size analizi):
```
udp-server.z1:  text=47485 B  data=536 B  bss=5768 B   (ROM'un %89.7'si dolu)
udp-client.z1:  text=47775 B  data=536 B  bss=5800 B   (ROM'un %90.3'ü dolu)
```

Cooja simülasyonunu başlatmak için:
```bash
java -jar contiki-ng/tools/cooja/dist/cooja.jar contiki-ng/examples/ota-firmware/BIL304-OS-Project-1.csc
```

---

## 7. Araştırma Görevi (ELF Analizi)

Bu projenin derlenmiş `new-firmware.z1` ELF dosyasının terminal üzerinden bellek, sembol ve mimari analizi yapılmış olup, teslim şablonu olarak belirlenen repoya fork'lanarak eklenmiştir.

> ELF analizine gitmek için repo linki: `[FORKLANAN REPO LİNKİ BURAYA EKLENECEKTİR]`
