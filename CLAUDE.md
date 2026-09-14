# Note per Claude

## Consegna delle build

Quando consegni una ROM all'utente, **consegna sempre la ROM completa (32 MB,
non tagliata) compressa in un archivio**. Niente ROM "trimmate": il padding va
rimosso solo dalla compressione, non dal file.

Motivo: il `.gba` prodotto da `make modern` è 32 MB di cui gran parte padding,
oltre il limite di invio; comprimendolo si resta ampiamente sotto il limite e
l'utente riceve comunque il file integrale.

```sh
make modern
zip -9 pokemonHnS_<descrizione>_<sha>.zip pokemonHnS.gba
```
