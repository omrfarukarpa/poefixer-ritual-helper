# Proje hafızası — Ritual Helper

> Güncellendi: 2026-09-15. Repository-owned context; secret içermez.

## Mevcut durum

- Kaynak ve README sürümü `1.5.4`; bu sürümde poe2scout JSON API host geçişi yer alıyor.
- v1.5.4 GitHub release yayımlandı: https://github.com/omrfarukarpa/poefixer-ritual-helper/releases/tag/v1.5.4. DLL SHA-256: `FD4B2B0D479974B4559E7F98666D10B994364C279AE32A5232950BB36D2E6840`.

## Onaylı kararlar ve sonuçlar

- Liga seçimi `league` alanında kalıcı; boş değer current softcore ligi seçer. Unique endpoint boş olsa da yerleşik katalog korunur.
- Kısmi/iptal fiyat yenilemesi geçerli kataloğu silmez. Event item'ları elle ekleme ve debug defer önizlemesi önceki yerel akışta mevcuttur.

## Riskler ve bekleyenler

- Poe2Scout canlı yanıtı, Ritual UI alanları ve oyun içi defer güvenilirliği doğrulanmadı. Otomatik test çalıştırılmadı.

