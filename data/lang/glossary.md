# GRAVE PRIX – glossary of fixed names

Use these names exactly, in both languages, so every writer stays consistent. Names of people are the same in
German and English; titles of things are translated.

## The firm and the press

| German | English | note |
|---|---|---|
| Graves & Sons | Graves & Sons | Mortimer Graves' funeral group; never "& Söhne". Running gag: there are no sons |
| Das Sensenblatt | The Grim Reader | the tabloid (Sensenblatt = scythe blade, Blatt = newspaper) |
| Radio Requiem | Radio Requiem | the race radio |
| Soll und Haben | Debit and Credit | the loan shark's twin debt collectors |
| Fahrerrabatt (zehn Prozent) | driver's discount (ten percent) | Graves' discount on every driver's funeral |

## Characters (`data/characters.txt`)

| key | name | role (DE) | role (EN) |
|---|---|---|---|
| graves | Mortimer Graves | Veranstalter, Inhaber von Graves & Sons | organiser, owner of Graves & Sons |
| mechanic | Lazarus Sprocket ("Lazarus") | Mechaniker | mechanic |
| dealer | Dolores Kaboom | Schwarzmarkt (strickende Oma mit Plätzchen und Minen) | black market (knitting granny with cookies and mines) |
| shark | Barnaby Fin | Kredithai | loan shark |
| editor | Hedda Epitaph | Chefredakteurin, Das Sensenblatt | editor-in-chief, The Grim Reader |
| announcer | Ricky Rattle | Stimme von Radio Requiem | the voice of Radio Requiem |

Graves says "Sie" to the player; everybody else says "du". The narration addresses the player as "du" / "you".

## Leagues (`data/leagues.txt`) – the higher the league, the better the grave

| league | German | English | venues |
|---|---|---|---|
| 5 | Armengrab | Pauper's Grave | junkyard, cemetery |
| 4 | Reihengrab | Death Row | docks, desert |
| 3 | Familiengruft | Family Plot | refinery, swamp |
| 2 | Staatsbegräbnis | Lying in State | downtown, pass |
| 1 | Pharaonengrab | Pharaoh's Tomb | fairground, proving |
| 0 (final) | Das letzte Geleit | The Last Procession | estate |

## Champions

| league | German = English |
|---|---|
| 5 | Barry D. Alive (gravedigger, bell on his wrist) |
| 4 | Jack Knife (trucker) |
| 3 | Petra Leum (oil baroness) |
| 2 | Gloria Mundy (pop diva; "Sic transit Gloria Mundy") |
| 1 | Major Casualty (retired officer) |
| boss | Mortimer Graves (armoured hearse `undertaker`) |

## Difficulties (`data/difficulty.txt`)

| # | German | English |
|---|---|---|
| 0 | Schonfrist | Grace Period |
| 1 | Blechschaden | Fender Bender |
| 2 | Totalschaden | Total Loss |
| 3 | Sargnagel | Coffin Nail |
| 4 | Totentanz | Danse Macabre |

## Sponsors (`data/sponsors.txt`)

| key | German | English | from league |
|---|---|---|---|
| crate_king | Kistenkönig Sarg-Discount | Crate Expectations Discount Caskets | 5 |
| wilting_lily | Blumen Welk | The Wilting Lily Florists | 5 |
| fine_print | Kleingedruckt Lebensversicherung | Fine Print Life Assurance | 4 |
| set_in_stone | Steinmetz Felsenfest | Set in Stone Monuments | 4 |
| cinderella | Krematorium Aschenputtel | Urn Your Keep Crematorium | 3 |
| rosy_cheeks | Thanatokosmetik Rosig | Rosy Cheeks Post-Mortem Beauty | 3 |
| pomp | Bestattungshaus Prunk & Pomp | Pomp & Circumstance Funerals | 2 |
| forever_fresh | Einbalsamierung Ewig Frisch | Forever Fresh Embalming | 2 |
| elysium | Gartenfriedhof Elysium | Elysian Acres Premium Cemetery | 1 |
| ebony_coffins | Sargmanufaktur Ebenholz | Mahogany Majesty Coffins | 1 |

## Words and conventions

- Money: the engine writes `{money}` as "12.345 $" (German) or "$12,345"; ads write sums the same way ("300 $" / "$300").
- Placeholders in news never take a gendered pronoun: {player} and {rival} may be anyone.
- German typography: „…“ for quotes in texts, – (en dash) with spaces for dashes; English ‘…’ inside quoted data,
  "…" only in `>` prose lines. Never "#" or "=" inside a quoted text.
- Recurring German terms: Leichenwagen (hearse), Grabstelle (burial plot), Nachruf (obituary), Kranz (wreath),
  Krähenfüße (spikes), Tretmine/Mine, Ölspur/Ölfleck (oil slick), Rauchwolke (smoke), Kopfgeld (bounty).
