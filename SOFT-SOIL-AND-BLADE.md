# Mjuk jord och sammanhängande bulldozerblad

Verifierat 2026-11-20. Underlag för prompt 3 och 4 i den bifogade uppdragsfilen.
Vid denna delkontroll var ändringarna lokala. Se `FINAL-GAMEPLAY-QA.md` för
efterföljande drivningskorrigeringar och slutkontrollens kvarstående acceptans.

## Marksimulering

- Våta material får lägre rasvinkel och kohesion. Tidigare ökade rasvinkeln
  med fukten, vilket kunde hålla våta högar onaturligt branta.
- Aktiv jord fördelas konservativt till upp till åtta grannkolumner.
  Flödet begränsas av tillgänglig referensvolym och aktuell höjdskillnad.
  Tidssteget tar hänsyn till när en budgeterad cell senast behandlades.
- Stabil jord lämnar arbetskön. Packning ändrar höjd, inte massa.
  Befintlig band-/hjulskjuvning och redskapskontakt väcker lokala områden.
- Intakt berg rasar inte som lös jord. Uppgrävt/tippat berg kan däremot rasa.
  Lagerflaggan `loose` sparas; äldre sparfiler utan flaggan kan fortfarande läsas.
- Närliggande terräng använder en gemensam yta med 0,25 m sampling för
  rendering och kontakt. Materialbokföringen behåller sina 1 m²-kolumner.
  LOD-kanter delar samma finare kantpunkter.

## Bulldozer

Den gamla konstruktionen roterade lyftarmarna och bladets övriga axlar
utifrån separata koordinatsystem. Jordbanken tog bara hänsyn till girning.

Nu följer lyftarmar, girled och blad samma kedja: rot → lyftarmar → girled
→ bladets rullning. Den gemensamma infästningen följer armarnas verkliga båge.
Hydraulikens ändpunkter följer lederna. En gemensam profil beskriver både
bladets synliga form och begränsningen för jordbanken framför plåten.
Skäreggens kontaktpunkter inkluderar lyftvinkeln. Automatisk bladhöjd löser
den nya geometrin numeriskt och behåller en fast schaktreferens.

## Verifiering

- Samtliga tio fristående fysiksviter passerade. Utökade jordprov passerade
  även för våt/torr lera, intakt/löst berg, upprepad hantering och vilande jord.
- Unreal Editor Win64 Development byggdes utan fel.
- Highlands-validering passerade: 27 kombinerade bladlägen, hydraulik,
  småhögar, schaktning, överlast, sidoförflyttning, markkontakt och återläsning.
- Schaktprovet nådde 7,05 m med 4,45 m³ tung jordbank och 0,331 m skärdjup.
  Efter höjning och avlastning fortsatte maskinen framåt. Provet kräver inte
  obegränsad framfart med ackumulerad last och oförändrat skärdjup.
- SoilLab passerade med bladvinklar −25°, 0° och +25°: schaktning, avlastning,
  lastarskopa, transport, tippning, lastbilsspår och sparning/återläsning.
  Rapporterat maximalt massfel var 0,000000000 ton.
- Grafisk körning med produktionsfunktionerna för körning och redskap
  passerade: ras, schaktning, lyft/avlastning, lastarskopa och tippning.
  Största bladlast var 5,170121 m³ och skoplast 1,725679 m³.
  Sex skärmbilder granskades. Tre ytterlägesbilder av bladet granskades också.
- Meshkontroll i fem områden fann inga degenererade eller vertikala trianglar
  eller ogiltiga normaler. 54 Chaos-strålar i sex områden passerade med största
  höjdavvikelsen 0,0000086 m.

Resultat och bilder finns under `Saved/SoftSoilBlade/`, särskilt
`Work-Verified`, `Blade-Final`, `QuarterMesh-Audit` och `QuarterMesh-Collision`.
Det grafiska provet kan upprepas med `tools/review-soft-soil.ps1 -Output`
och en ny utdatakatalog.

## Avgränsningar

Detta är en lagerindelad höjdfältsmodell med lokal volymtransport, inte
partikelbaserad geoteknik eller en fullständig 3D-vätska. Överhäng och tunnlar
stöds inte. Parametrarna är inte laboratoriekalibrerade. De grafiska
arbetsproven använder styrda anrop till maskinernas verkliga kör-/arbetskod;
de verifierar inte samtliga tangentbords- och muskontroller. Finare nät kan
öka renderings- och kollisionskostnaden; 4K/120 fps har inte verifierats här.
