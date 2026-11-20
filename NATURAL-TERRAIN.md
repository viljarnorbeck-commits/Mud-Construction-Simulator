# Naturlig terräng – uppdrag 1 och 2

Den aktiva 2 km-världen kom från `generate-highlands.mjs --earthworks` och
`earthworks-terrain.mjs`, medan standardkommandot `generate-world.mjs` skapade
en annan 1 km-värld. Windows-bygget kopierade samtidigt `world/earthworks-42`.
Detta är nu samordnat: `node tools/generate-world.mjs 42`, byggskriptet och
standardstarten använder `world/42`. Den gamla generatorn finns via `--legacy`;
den tidigare earthworks-kartan finns kvar som ett uttryckligt startalternativ.

Grundorsaken till jordväggarna var avsiktliga mycket smala höjdövergångar:
18 m terrasshöjd på 4 m bredd, smala ravinsidor, en nästan stegformad målterrass
och bäckskärning med fast sidobredd oavsett dalens djup. Ett befintligt test
krävde också en jordslänt med mer än 300 % lutning. Den aktuella `BuildTile`
använde redan gemensamma halvmeterkanter och inga vertikala skirts.

## Ändrat

- Terrass-, ravin- och bäcksidor har sammanhängande, bredare övergångar.
  Bäckdalens sidobredd följer den faktiska skärningshöjden.
- Domain warping, fleroktaviga bergsryggar och anslutna avrinningsstråk behålls.
  Materialberoende lutningskontroll i 16 riktningar tar bort orealistiskt
  understödda höjdkanter utan att sudda ut hela kartan.
- Bergzonernas tillåtna branthet tonas in mot omgivningen. Flackare bergshyllor
  får ett faktiskt 0,45 m jordlager över berg, i stället för ett helt grått område
  avgränsat av en cirkel. Jord-, lera- och sandgränser skiljer sig från berg.
- Målterrassen behåller ett sammanhängande, brant men bearbetbart hinder;
  spelaren kan schakta en ramp eller bygga fyllning på flera ställen.
- Ruttsökningens test tar nu även hänsyn till lastbilens befintliga gräns för
  sidolutning. Det gamla testet kunde annars sicksacka uppför en slänt som
  skulle välta det verkliga lastade fordonet.
- Spelvyns uppdragstext känner igen både äldre och nya earthworks-revisioner,
  så v4 inte felaktigt får den gamla vadbeskrivningen.

Lutningskontrollen sker när grundvärlden genereras. Den är en geometrisk
höjdomslutning mot dräneringen, **inte** en hydraulisk fluidlösare eller ett
massbevarande ras i det pågående spelet. Den ändrar inga sparade jordkolumner.
Det befintliga massbevarande materialflödet under spel används fortsatt.

## Sparningar och aktiva data

Nya världen heter `earthworks-v4-42` och får egen lokal profil. Gamla
`earthworks-v3-42`-sparningar blandas inte med de nya höjderna eller trädindexen.
Det finns en strikt worldId-kontroll vid återläsning. Gamla världsfiler lämnas
kvar under `world/earthworks-42`; `tools/start-highlands.ps1 -World earthworks-42`
kan välja den äldre världen igen. Detta kopierar vald karta till Content/World,
så starta inte olika världar samtidigt från samma projekt.

Tidigare lokala `world/42`- och `Content/World`-data säkerhetskopierades till
`Saved/NaturalTerrain-Before` innan genereringen. Granskningsläget laddar eller
sparar aldrig användarens profil. Det är inte en migrering av gamla jordarbeten
till den nya kartan.

## Verifierat lokalt 2026-11-20

- Editor-bygget kompilerar och länkar. Grafisk Unreal-start lyckades.
- Slutlig karta: cirka 6,6–309,4 m höjd, 19 609 träd och 4 139 stenblock.
  Cirka 40 % av testade punkter har mer än 70 % lutning och 38 % mindre än 15 %;
  landskapet har alltså både svåra och relativt enkla områden.
- Höjdkartstestet hittade inga nästan lodräta steg. Största uppmätta
  jordhöjdskillnaden i dess delprov var 0,822 m per meter. Brantare berg finns
  kvar i begränsade områden. Åar, ravindjup och plana arbetsplatser kontrolleras.
- Samtliga tio fristående fysiksviter gick igenom. Efter sista kartjusteringen
  kördes berört ruttest igen: lastad lastbil rullade tillbaka 6,73 m på obearbetad
  terräng och körde 466,02 m efter schakt/fyllning/packning. Det är ett automatiskt
  fysikprov på den riktiga kodade höjdprofilen; inte ett manuellt genomfört
  466 m vägbyggnadsprojekt. Massfelet var mindre än 0,000001 ton.
- Native mesh/LOD-test i fem områden: 42 766 prov, inga vertikala/degenererade
  trianglar, inga ogiltiga normaler och kontakt-/renderhöjdfel utskrivet som noll.
- Grafisk kameragenomgång från marknivå och ovanifrån i sex områden. Alla sex
  slutliga bilder granskades: startslänt, ravin, målterrass, västra ryggar,
  bäckdal och östra berg. Skärmbilder finns i `Saved/NaturalTerrainReview/Final`;
  före-bilder finns i motsvarande `Before`-mapp.
- 54 riktiga Chaos-raycasts i dessa sex områden träffade den synliga marken;
  största höjdfelet var 0,0000082 m. Kollisionerna har inte stängts av.
- I det grafiska testläget kördes produktionsmaskinerna på separata körstråk:
  bulldozer 3,01 m, hjullastare 5,94 m och lastbil 8,87 m. Detta verifierar
  grundläggande körning, inte att alla hinder eller maskinuppgifter är testade.
- Hashen för `Content/World/height.png` matchade `world/42/height.png`, och
  startloggen bekräftade worldId `earthworks-v4-42` och 2049×2049 prov.

Upprepa efterkontrollen med:

```powershell
node tools/generate-world.mjs 42
node tests/earthworks-world.mjs
# Bygg/kopiera vald värld innan grafiskt prov:
.\tools\build-windows.ps1 -Target Editor
.\tools\review-terrain.ps1 -Output Saved/TerrainReview-New -Audit
.\tools\review-terrain.ps1 -Output Saved/TerrainViews-New
```

Trädmodeller, materialens detaljgrafik och enstaka stora LOD-facetter kan fortfarande
förbättras. Ingen fullständig gameplaybalansering eller ny prestandagaranti ingår.
Ingen commit eller push har gjorts; detta ska ingå i den gemensamma leveransen.
