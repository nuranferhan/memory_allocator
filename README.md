# Custom Memory Allocator

## Amaç

Bu proje, C ve POSIX/Linux API kullanılarak standart `malloc`, `free` ve
`calloc` mantığını taklit eden, kullanıcı alanında çalışan bir bellek yöneticisi
geliştirmeyi amaçlamaktadır. Temel hedef; bellek bloğu yönetimi, yerleştirme
stratejileri, fragmentation analizi, hata denetimi, thread-safe erişim ve
kullanıcı dostu bir terminal arayüzü üzerinden allocator davranışını
gözlemlemektir.

---

## Projedeki Dosyalar

| Dosya | Açıklama |
|-------|----------|
| `allocator.c` | Allocator algoritmaları, malloc/free/calloc/realloc ve tanılama fonksiyonları |
| `allocator.h` | Veri yapıları, sabitler, API prototipleri ve snapshot yapısı |
| `log.c` / `log.h` | Thread-safe log sistemi |
| `test.c` | Allocator doğrulama testleri |
| `benchmark.c` | Performans ve fragmentation benchmarkları |
| `tui.c` | Türkçe destekli terminal kullanıcı arayüzü |
| `Makefile` | Derleme, test, benchmark, TUI ve Valgrind hedefleri |

---

## Tasarım

### Heap Yapısı

Bellek yöneticisi, BSS bölümünde tanımlanmış `16 MB` boyutunda statik bir
diziden (`static_heap[]`) yararlanır. `sbrk` veya `mmap` yerine statik alanın
tercih edilmesinin nedeni, bellek sınırlarının açık biçimde denetlenebilmesi ve
allocator davranışının daha kolay test edilebilmesidir.

```text
+--------------------------------------------------------------+
|                 static_heap[HEAP_SIZE = 16 MB]               |
+----------+-----------+----------+-----------+----------------+
| header   | veri      | header   | veri      | ...            |
|          | kullanıcı |          | kullanıcı |                |
|          | alanı     |          | alanı     |                |
+----------+-----------+----------+-----------+----------------+
```

Her blok bir `block_header_t` yapısıyla başlar:

| Alan | Açıklama |
|------|----------|
| `size` | Kullanıcıya döndürülen net bayt sayısı |
| `magic` | `0xCAFEBABE` dolu, `0xDEADBEEF` serbest blok kontrolü |
| `is_free` | Serbest/dolu bayrağı |
| `next/prev` | Çift bağlı serbest liste göstericileri |
| `next_phys` | Fiziksel bellekte bir sonraki blok |
| `prev_phys` | Fiziksel bellekte bir önceki blok |

### Serbest Liste Yönetimi

- Serbest bloklar çift bağlı listede tutulur.
- Yeni serbest blok liste başına `O(1)` maliyetle eklenir.
- Uygun blok arama First-Fit veya Best-Fit stratejisine göre `O(n)` yapılır.
- Serbest bırakılan bloklar fiziksel komşularıyla birleştirilerek dış
  fragmentation azaltılır.

### Yerleştirme Stratejileri

**First-Fit:** Yeterli büyüklükteki ilk serbest blok seçilir. Arama erken
sonlanabildiği için hızlıdır.

**Best-Fit:** Yeterli büyüklükteki en küçük serbest blok seçilir. Listeyi daha
fazla tarar, ancak bazı senaryolarda boş alanı daha ekonomik kullanabilir.

### Splitting ve Coalescing

- **Splitting:** Bulunan blok, istekten belirgin biçimde büyükse ikiye bölünür.
  Kullanılmayan parça serbest listeye eklenir.
- **Coalescing:** Bir blok serbest bırakıldığında fiziksel olarak bitişik
  serbest komşularla birleştirilir. Hem önceki hem sonraki fiziksel blok kontrol
  edilir.

### Hizalama

Tüm tahsisler 16-byte sınırına yuvarlanır:

```c
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
```

Bu, bellek erişimlerinin düzenli olmasını ve modern işlemcilerde daha uyumlu
çalışmasını sağlar.

---

## Yeni TUI Arayüzü

Projeye `tui.c` dosyası ile terminal kullanıcı arayüzü eklenmiştir. TUI,
allocator durumunu görsel olarak izlemeyi ve bazı işlemleri klavyeden doğrudan
denemeyi sağlar.

### TUI Özellikleri

- `make tui` ile terminal ekranı açılır.
- Türkçe metinler UTF-8 locale ile yazdırılır.
- Ok tuşları ve tek karakterli komutlar ayrı ayrı algılanır.
- UTF-8 karakterlerin devam baytları tek giriş olarak tüketilir; klavyeden her
  karakterin yanlışlıkla birden fazla komut gibi algılanması engellenir.
- Arka planda rastgele harf, sembol veya göz yoran süsleme kullanılmaz.
- Sekmeler ve bilgilendirme mesajları okunabilir renklerle gösterilir.
- Her sekmede o sekmenin ne yaptığı kısa bir açıklamayla sunulur.

### TUI Sekmeleri

| Sekme | İçerik |
|-------|--------|
| `Genel` | Heap boyutu, anlık kullanım, zirve kullanım, tahsis/free sayıları, fragmentation |
| `Bloklar` | Serbest blok sayısı, en büyük serbest blok, örnek tahsis listesi |
| `Testler` | Etkileşimli malloc, calloc, free, double-free ve thread testi komutları |
| `Yardım` | Kısayollar ve değerlerin anlamı |

### TUI Kısayolları

| Tuş | İşlem |
|-----|------|
| `1-4` | Sekme seçimi |
| `Sol/Sağ ok` | Önceki/sonraki sekmeye geçiş |
| `a` | Örnek `my_malloc` çağrısı |
| `c` | Örnek `my_calloc` çağrısı ve sıfırlama kontrolü |
| `f` | Son örnek bloğu `my_free` ile serbest bırakma |
| `d` | Double-free koruma testi |
| `t` | Çok threadli tahsis/free testi |
| `r` | Allocator durumunu sıfırlama |
| `q` | TUI çıkışı |

---

## Kullanılan Sistem Programlama Kavramları

| Kavram | Kullanım Yeri |
|--------|---------------|
| `pthread_mutex_t` | Allocator veri yapıları ve log sistemi için thread güvenliği |
| `pthread_create/join` | Test, benchmark ve TUI thread testi |
| `clock_gettime` | Test ve benchmark süre ölçümü |
| `termios` | TUI içinde tek tuş/ham terminal girişi |
| `ioctl(TIOCGWINSZ)` | TUI ekran genişliği/yüksekliği okuma |
| `setlocale` | Türkçe karakterlerin terminalde doğru gösterilmesi |
| `errno` | Bellek tükenmesi durumunda `ENOMEM` bildirimi |
| Magic sayı | Double-free ve geçersiz blok kontrolü |
| Çift bağlı liste | Serbest blokların yönetimi |

---

## Çalıştırma Adımları

### Gereksinimler

```text
gcc >= 7.0
make
pthreads / libpthread
Kali Linux, Ubuntu, Debian veya benzeri POSIX/Linux ortamı
valgrind (sadece make valgrind için)
```

Windows üzerinde MinGW ile `pthread.h`, `termios.h` ve Valgrind desteği
bulunmayabilir. Bu nedenle proje Linux/Kali/Debian ortamında çalıştırılmalıdır.

### Derleme

```bash
cd memory_allocator-main
make all
```

`make all` şu ikili dosyaları üretir:

```text
allocator_test
allocator_bench
allocator_tui
```

### Test Programını Çalıştırma

```bash
make test
```

veya:

```bash
./allocator_test
```

### Benchmark Programını Çalıştırma

Bu projede benchmark hedefinin adı `bench` olarak tanımlıdır:

```bash
make bench
```

veya:

```bash
./allocator_bench
```

### TUI Arayüzünü Çalıştırma

```bash
make tui
```

veya:

```bash
./allocator_tui
```

### Valgrind ile Bellek Analizi

```bash
make valgrind
```

Bu hedef `allocator_test` programını Valgrind altında çalıştırır ve çıktıyı
`valgrind_report.txt` dosyasına yazar.

Valgrind kurulu değilse Debian/Kali/Ubuntu üzerinde:

```bash
sudo apt update
sudo apt install valgrind
```

### Temizleme

```bash
make clean
```

---

## Testler

| No | Test Adı | Doğruladığı Özellik |
|----|----------|---------------------|
| T1 | Temel malloc/free | Tahsis, yazma/okuma bütünlüğü, serbest bırakma |
| T2 | calloc sıfırlama | Tahsis edilen belleğin sıfırlanması |
| T3 | realloc büyütme/küçültme | Boyut değişiminde veri koruması |
| T4 | Double-free koruması | Çifte serbest bırakmanın engellenmesi |
| T5 | Hizalama doğrulaması | 16-byte hizalama garantisi |
| T6 | Fragmentation karşılaştırması | First-Fit ve Best-Fit fragmentation oranları |
| T7 | Thread güvenliği | Çok threadli erişimde veri yapısının bozulmaması |
| T8 | Bellek tükenmesi | OOM durumunda `NULL` dönüşü |
| T9 | Performans ölçümü | Malloc/free döngüsü süresi |
| T10 | Coalescing doğrulaması | Bitişik serbest blokların birleştirilmesi |

---

## Benchmark Senaryoları

| No | Senaryo | Ölçülen Metrik |
|----|---------|----------------|
| B1 | First-Fit vs Best-Fit | 10.000 işlemde geçen süre |
| B2 | Thread ölçeklenebilirliği | 1/2/4/8/16 thread throughput |
| B3 | Blok boyutu etkisi | 8B-4KB arası alloc/free süresi |
| B4 | Fragmentation yük altında | Dolu ortamda fragmentation oranı |

---

## Fragmentation Hesabı

Allocator fragmentation oranını serbest liste üzerinden hesaplar:

```text
fragmentation = 1 - (en büyük serbest blok / toplam serbest byte)
```

Örnek:

- Toplam serbest alan: 1000 byte
- En büyük serbest blok: 700 byte
- Fragmentation: `1 - 700 / 1000 = 0.30`, yani `%30`

Bu değer büyüdükçe serbest alanın daha parçalı olduğu anlaşılır.

---

## Hata Kontrolleri

### Double-Free Kontrolü

`my_free` çağrısında blok başlığındaki `magic` ve `is_free` alanları kontrol
edilir. Blok zaten serbestse işlem durdurulur ve hata log dosyasına yazılır.

### Geçersiz Pointer Kontrolü

Serbest bırakılmak istenen adresin heap sınırları içinde olup olmadığı kontrol
edilir. Heap dışındaki adresler serbest listeye alınmaz.

### calloc Taşma Kontrolü

`my_calloc(nmemb, size)` içinde `nmemb * size` çarpımının taşma üretip
üretmediği kontrol edilir. Taşma varsa `NULL` döndürülür ve `errno = ENOMEM`
ayarlanır.

### Thread Güvenliği

Allocator içindeki serbest liste, istatistikler ve blok yapıları
`pthread_mutex_t` ile korunur. Böylece aynı anda birden fazla thread tahsis ve
serbest bırakma yaptığında veri yapısının bozulması engellenir.

---

## Karşılaşılan Problemler ve Çözümler

### 1. Fiziksel Komşu Takibi

**Problem:** Sadece serbest liste bağlantıları tutulduğunda coalescing sırasında
fiziksel komşuları bulmak için heap taraması gerekiyordu.

**Çözüm:** `next_phys` ve `prev_phys` göstericileri `block_header_t` yapısına
eklendi. Böylece bitişik blok kontrolü doğrudan yapılabiliyor.

### 2. Thread Güvenliğinde Deadlock Riski

**Problem:** `my_realloc` içinde hem `my_malloc` hem `my_free` çağrıldığı için
kilidin yanlış sırada alınması deadlock riski oluşturabilirdi.

**Çözüm:** Eski blok boyutu kilit altında okunur, sonra kilit bırakılır.
Ardından yeni tahsis ve eski bloğu serbest bırakma işlemleri normal API
üzerinden yapılır.

### 3. Double-Free Sessiz Bozulma

**Problem:** Double-free serbest listeyi bozabilir ve sonraki tahsislerde
tanımsız davranışa yol açabilir.

**Çözüm:** `MAGIC_ALLOC`, `MAGIC_FREE` ve `is_free` alanlarıyla double-free
yakalanır. Hata log'a yazılır ve serbest liste değiştirilmez.

### 4. TUI İçin Güvenli İstatistik Okuma

**Problem:** TUI, allocator içindeki verileri doğrudan okusaydı eş zamanlı
erişimde tutarsız veri görebilirdi.

**Çözüm:** `allocator_get_snapshot` fonksiyonu eklendi. Bu fonksiyon mutex
altında heap istatistiklerini tek bir snapshot yapısına kopyalar.

### 5. Türkçe Karakter ve Klavye Girişi

**Problem:** Terminalde Türkçe karakterler ve ok tuşları yanlış algılanabilir.

**Çözüm:** TUI içinde `setlocale`, `termios` ham giriş modu ve UTF-8 devam baytı
tüketimi kullanıldı. Böylece her tuş tek giriş olarak işlenir.
