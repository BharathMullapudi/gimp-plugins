#include "Languages.h"

char * msg[4][104] =
{

/* English messages */
{
  "OK",
  "Cancel",
  "Delete fractal",
  "Are you sure you want to delete",
  "\"%s\" from the list and from disk?",
  "Delete",
  "Error opening: %s",
  "File '%s' Not a FractalExplorer file",
  "File '%s' corrupt file - Line %d Option section incorrect",
  "Internal error - list item has null object!",
  "Unknown event\n",
  "Edit fractal name",
  "Fractal name:",
  "New fractal",
  "%s [copy]",
  "Save",
  "Save as...",
  "Copy",
  "Load",
  "No fractalexplorer-path in gimprc:\n"
      "You need to add an entry like\n"
      "(fractalexplorer-path \"${gimp_dir}/fractalexplorer:${gimp_data_dir}/fractalexplorer\n"
      "to your ~/.gimprc/gimprc file\n",
  "fractalexplorer-path miss-configured - \nPath `%.100s' not found\n",
  "Entry %.100s is not a directory\n",
  "error reading fractalexplorer directory \"%s\"",
  "My first fractal",
  "Choose fractal by double-clicking on it",
  "Rescan",
  "Select directory and rescan collection",
  "New",
  "Create a new fractal for editing",
  "Rename",
  "Rename fractal in list",
  "Delete currently selected fractal",
  "Choose gradient by double-clicking on it",
  "Add FractalExplorer path",
  "Rescan for fractals",
  "Add dir",
  "Parameters",
  "Parameters",
  "Change the first (minimal) x-coordinate delimitation",
  "Change the second (maximal) x-coordinate delimitation",
  "Change the first (minimal) y-coordinate delimitation",
  "Change the second (maximal) y-coordinate delimitation",
  "Change the iteration value. The higher it is, the more details will be calculated, which will take more time.",
  "Change the CX value (changes aspect of fractal, active with every fractal but Mandelbrot and Sierpinski)",
  "Change the CY value (changes aspect of fractal, active with every fractal but Mandelbrot and Sierpinski)",
  "Reset parameters to default values",
  "Load a fractal from file",
  "Save active fractal to file",
  "Fractal type",
  "Options",
  "Reset to default values",
  "Preview options",
  "Realtime preview",
  "If you enable this option the preview will be redrawn automatically.",
  "Redraw",
  "Redraw preview",
  "Zoom options",
  "Undo zoom",
  "Undo last zoom",
  "Redo zoom",
  "Redo last zoom",
  "Step in",
  "Step out",
  "Colors",
  "Color density",
  "Red",
  "Green",
  "Blue",
  "Change the intensity of the red channel",
  "Change the intensity of the green channel",
  "Change the intensity of the blue channel",
  "Color function",
  "Sine",
  "Cosine",
  "None",
  "Use sine-function for this color component",
  "Use cosine-function for this color component",
  "Use linear mapping instead of any trigonometrical function for this color channel",
  "Inversion",
  "If you enable this option higher color values will be swapped with lower ones and vice versa.",
  "Color mode",
  "As specified above",
  "Create a color-map with the options you specified above (color density/function). The result is visible in the preview image",
  "Apply active gradient to final image",
  "Create a color-map using a gradient from the gradient editor.",
  "Gradients",
  "Fractals",
  "Accept settings and start the calculation of the fractal",
  "Discard any changes and close dialog box",
  "About",
  "Show information about the plug-in and the author",
  "This will close the information box",
  "Error opening '%.100s' could not save",
  "Failed to write file\n",
  "Save: No filename given",
  "Save: Can't save to a directory",
  "Load fractal parameters",
  "Click here to load your file.",
  "Click here to cancel load procedure.",
  "Save fractal parameters",
  "Click here to save your file.",
  "Click here to cancel save procedure.",
  "Save settings",
  "This saves the currently selected language to the configuration file",
},


/* Messages en fran�ais */
{
  "Accepter",
  "Annuler",
  "Effacer fractal",
  "Etes-vous sur de vouloir effacer le fichier",
  "\"%s\" de la liste est du disque?",
  "Effacer",
  "Erreur lors de l'ouverture du fichier: %s",
  "Le fichier '%s' ne correspond pas au format FractalExplorer!",
  "Le fichier '%s' est corrompu - Ligne %d incorrecte.",
  "Erreur interne - l'�l�ment de la liste contient un objet NULL!",
  "Ev�nement inconnu.\n",
  "Changer le nom du fractal",
  "Nom du fractal:",
  "Nouveau fractal",
  "%s [copie]",
  "Enregistrer",
  "Enregistrer sous...",
  "Copier",
  "Charger",
  "Aucun r�pertoire FractalExplorer inscrit dans gimprc:\n"
      "Vous devez aujouter une ligne telle que\n"
      "(fractalexplorer-path \"${gimp_dir}/fractalexplorer:${gimp_data_dir}/fractalexplorer\n"
      "dans votre fichier ~/.gimprc/gimprc\n",
  "fractalexplorer-path n'est pas configur� correctement - \nDossier `%.100s' introuvable.\n",
  "L'entr�e %.100s n'est pas un r�pertoire.\n",
  "Erreur lors de la lecture du r�pertoire \"%s\".",
  "Mon premier fractal",
  "Choisissez un fractal en double-cliquant sur celui-ci",
  "Rafra�chir",
  "Choix du r�pertoire et rechargement de la collection.",
  "Nouveau",
  "Cr�er nouveau fractal.",
  "Renommer",
  "Renommer le fractal contenu dans la liste.",
  "Efface le fractal actuellement selection� dans la liste.",
  "Choisissez un d�grad� de couleurs avec un double-click",
  "Ajouter un dossier FractalExplorer.",
  "Rechargement de nouveaux fractals.",
  "Ajout de r�pertoires",
  "Param�tres",
  "Param�tres",
  "Change la premi�re delimitation de l'axe x (delimitation minimale de x).",
  "Change la deuxi�me delimitation de l'axe x (delimitation maximale de x).",
  "Change la premi�re delimitation de l'axe y (delimitation minimale de y).",
  "Change la deuxi�me delimitation de l'axe y (delimitation maximale de y).",
  "Change la valeur d'iteration. Une valeur plus haute rend l'image plus detail�e, mais utilise plus de temps de calcul.",
  "Change la valeur CX (cela change l'aspect du fractal; l'option n'est pas active pour les types de fractal Mandelbrot and Sierpinski).",
  "Change la valeur CY (cela change l'aspect du fractal; l'option n'est pas active pour les types de fractal Mandelbrot and Sierpinski).",
  "Remets tous les param�tres � leur valeur par d�faut.",
  "Charge un fractal � partir d'un fichier.",
  "Enregistre un fractal dans un fichier.",
  "Type de fractal",
  "Configuration",
  "Valeurs par d�faut",
  "Pr�visualisation",
  "En temps r�el",
  "Si vous activez cette option, la fen�tre de pr�visualisation sera automatiquement rafra�chie apr�s chaque changement d'option.",
  "Actualiser",
  "Actualise et redessine la pr�visualisation.",
  "Options de zoom",
  "Annuler zoom",
  "Ceci annule le dernier zoom.",
  "Refaire zoom",
  "Ceci annule la derni�re annulation du zoom.",
  "Se rapprocher",
  "S'�loigner",
  "Couleurs",
  "Densit� des valeurs RVB d'une couleur",
  "Rouge",
  "Vert",
  "Bleu",
  "Change l'intensit� du canal rouge.",
  "Change l'intensit� du canal vert.",
  "Change l'intensit� du canal bleu.",
  "Fonctions appliqu�s aux canaux RVB",
  "Sinus",
  "Cosinus",
  "Aucun",
  "Utilise la fonction trigonom�trique 'sinus' pour le calcul de cette composante de couleur.",
  "Utilise la fonction trigonom�trique 'cosinus' pour le calcul de cette composante de couleur.",
  "Utilise une fonction lin�aire au lieu d'une fonction trigonom�trique pour ce canal.",
  "Inversion",
  "En activant cette option, vous assignez de grandes valeurs de couleurs aux couleurs ayant re�u des valeurs petites et vice versa.",
  "Mode de couleur",
  "Comme specifi� ci-dessus",
  "Cr�e une palette de couleur en utilisant les options que vous avez choisies ci-dessus (densit�/fonction). Le r�sultat est visible dans l'image de pr�visualisation.",
  "Applique le d�grad� de couleur actif",
  "Cr�e une palette en utilisant le d�grad� de couleur du 'Gradient Editor'.",
  "D�grad�s",
  "Fractals",
  "Accepte les options et d�marre la calculation du fractal.",
  "Rejette tout changement et ferme la bo�te de dialogue.",
  "Info...",
  "Affiche des informations concernant l'auteur et le plug-in.",
  "Ceci fermera la bo�te de dialogue d'info.",
  "Erreur lors de l'ouverture de '%.100s'. Sauvegarde echou�e.",
  "Ecriture du fichier impossible.\n",
  "Enregistrement: Aucun fichier specifi�.",
  "Enregistrement: Impossible de sauvegarder dans un r�pertoire.",
  "Chargement des param�tres d'un fractal",
  "Cliquez ici afin de charger un fichier FractalExplorer.",
  "Cliquez ici pour interrompre la proc�dure de chargement.",
  "Enregistrement des param�tres d'un fractal",
  "Cliquez ici pour charger votre fichier.",
  "Cliquez ici pour imterropre la proc�dure d'enregistrement.",
  "Enregistrer langue",
  "Ceci enregistre la langue actuelle dans le fichier de configuration.",
},

/* Deutsche Mitteilungen */
{
  "Weiter",
  "Abbrechen",
  "Fraktal l�schen",
  "Sind sie sicher, dass sie die Datei",
  "\"%s\" aus der Liste und von der Festplatte entfernen m�chten?",
  "L�schen",
  "Fehler beim �ffnen der Datei: %s",
  "Die Datei '%s' scheint nicht im FractalExplorer-Format zu sein!",
  "Die Datei '%s' ist besch�digt - Zeile %d inkorrekt.",
  "Interner Fehler - das Listenelement besitzt ein NULL Objekt!",
  "Unbekanntes Ereignis.\n",
  "�ndere Fraktalnamen",
  "Fraktal-Name:",
  "Neues Fraktal",
  "%s [Kopie]",
  "Speichern",
  "Speichern als...",
  "Kopie",
  "Laden",
  "Kein fractalexplorer-path in gimprc:\n"
      "Sie m�ssen einen Eintrag wie der folgende in ihre ~/.gimprc/gimprc Datei einf�gen:\n"
      "(fractalexplorer-path \"${gimp_dir}/fractalexplorer:${gimp_data_dir}/fractalexplorer\n",
  "fractalexplorer-path falsch konfiguriert - \nPfad `%.100s' nicht gefunden\n",
  "Eintrag %.100s ist kein Verzeichnis.\n",
  "Fehler beim Lesen des FractalExplorer-Verzeichnisses \"%s\".",
  "Mein erstes Fraktal",
  "W�hlen Sie ein Fraktal durch Doppelklick aus",
  "Aktualisieren",
  "Wahl eines Verzeichnisses und Wiedereinlese der Dateisammlung.",
  "Neu",
  "Erstellt ein neues Fraktal.",
  "Umbenennen",
  "Benennt das Fraktal in der Liste um.",
  "L�scht das gerade gew�hlte Fraktal der Liste.",
  "W�hlen Sie einen Farbverlauf durch Doppelklick aus",
  "F�ge FractalExplorer-Pfad hinzu",
  "Nach neuen Fraktalen suchen",
  "Neues Verzeichis hinzuf�gen.",
  "Fraktal-Optionen",
  "Parameter",
  "�ndert die erste (minimale) Begrenzung der x-Koordinate.",
  "�ndert die zweite (maximale) Begrenzung der x-Koordinate.",
  "�ndert die erste (minimale) Begrenzung der y-Koordinate.",
  "�ndert die zweite (maximale) Begrenzung der y-Koordinate.",
  "�ndert die Iterations-Variable. Je h�her sie ist, um so genauer und detailierter wird das Bild sein. Eine gr�ssere Berechenzeit ist allerdings in Kauf zu nehmen.",
  "�ndert den CX-Wert (Dies wirkt sich auf alle Fraktale aus, ausser Mandelbrot und Sierpinski).",
  "�ndert den CY-Wert (Dies wirkt sich auf alle Fraktale aus, ausser Mandelbrot und Sierpinski).",
  "Setze Parameter auf die Standardwerte zurueck.",
  "Lade ein Fraktal aus einer Datei",
  "Speichere das aktive Fraktal in eine Datei",
  "Fraktal-Typ",
  "Diverse Optionen",
  "Zur�cksetzen",
  "Vorschau-Optionen",
  "Echtzeit-Vorschau",
  "Falls Sie diese Option aktivieren, wird das Vorschaufenster stets automatisch aktualisiert.",
  "Neu zeichnen",
  "Zeichnet die Vorschau neu",
  "Zoom-Optionen",
  "Rueckg�ngig",
  "Macht den letzten Zoom-Vorgang wieder r�ckg�ngig.",
  "Wiederherstellen",
  "Stellt den letzten Zoom-Vorgang wieder her.",
  "Hinein",
  "Hinaus",
  "Farb-Optionen",
  "Farbintensit�t",
  "Rot",
  "Gruen",
  "Blau",
  "�ndert die Intensit�t des roten Kanals.",
  "�ndert die Intensit�t des gruenen Kanals.",
  "�ndert die Intensit�t des blauen Kanals.",
  "Farb-Funktion",
  "Sinus",
  "Cosinus",
  "Keine",
  "Verwende Sinus-Funktion f�r diese Farbkomponente.",
  "Verwende Cosinus-Funktion f�r diese Farbkomponente.",
  "Verwende lineare Farbabstufung statt einer trigonometrischen Funktion.",
  "Inversion",
  "Falls Sie diese Option aktivieren, werden tiefere Farbwerte durch h�here ausgetauscht und umgekehrt.",
  "Farb-Modus",
  "Wie oben stehend angegeben",
  "Berechne Farbpalette mit den oben angegebenen Optionen (Farb-Intensit�t/-Funktion). Das Resultat ist in der Vorschau sichtbar.",
  "Wende aktiven Farbverlauf an",
  "Berechne Farbpalette mit den Angaben eines Verlaufes aus dem Gradient-Editor.",
  "Farbverl�ufe",
  "Fraktale",
  "Akzeptiere Einstellungen und starte die Berechnung des Fraktals.",
  "Verwerfe jegliche �nderungen und schliesse das Fenster.",
  "�ber...",
  "Zeige Informationen �ber den Autor und das Plug-In.",
  "Info-Box schliessen",
  "Fehler beim �ffnen von '%.100s'. Konnte nicht speichern",
  "Speichern der Datei fehlgeschlagen\n",
  "Speichern: Keine Datei angegeben",
  "Speichern: Kann nicht in ein Verzeichnis speichern",
  "Lade Parameter eines Fraktals",
  "Klicken Sie hier, um das Fraktal zu laden.",
  "Klicken Sie hier, um den Ladevorgang abzubrechen.",
  "Speichere Fraktalparameter",
  "Klicken Sie hier, um das Fraktal in eine Datei zu speichern.",
  "Klicken Sie hier, um den Speicherungsvorgang abzubrechen.",
  "Sprache abspeichern",
  "Klicken Sie hier, um die gewaehlte Sprache als Standard zu definieren und in die Konfigurationsdatei abzuspeichern.",
},

/* Schwedische Mitteilungen 
   ========================
   Hier koenntest Du die schwedischen Texte einfuegen, d.h. die deutschen ueberschreiben.
*/

{
  "Weiter",
  "Abbrechen",
  "Fraktal l�schen",
  "Sind sie sicher, dass sie die Datei",
  "\"%s\" aus der Liste und von der Festplatte entfernen m�chten?",
  "L�schen",
  "Fehler beim �ffnen der Datei: %s",
  "Die Datei '%s' scheint nicht im FractalExplorer-Format zu sein!",
  "Die Datei '%s' ist besch�digt - Zeile %d inkorrekt.",
  "Interner Fehler - das Listenelement besitzt ein NULL Objekt!",
  "Unbekanntes Ereignis.\n",
  "�ndere Fraktalnamen",
  "Fraktal-Name:",
  "Neues Fraktal",
  "%s [Kopie]",
  "Speichern",
  "Speichern als...",
  "Kopie",
  "Laden",
  "Kein fractalexplorer-path in gimprc:\n"
      "Sie m�ssen einen Eintrag wie der folgende in ihre ~/.gimprc/gimprc Datei einf�gen:\n"
      "(fractalexplorer-path \"${gimp_dir}/fractalexplorer:${gimp_data_dir}/fractalexplorer\n",
  "fractalexplorer-path falsch konfiguriert - \nPfad `%.100s' nicht gefunden\n",
  "Eintrag %.100s ist kein Verzeichnis.\n",
  "Fehler beim Lesen des FractalExplorer-Verzeichnisses \"%s\".",
  "Mein erstes Fraktal",
  "W�hlen Sie ein Fraktal durch Doppelklick aus",
  "Aktualisieren",
  "Wahl eines Verzeichnisses und Wiedereinlese der Dateisammlung.",
  "Neu",
  "Erstellt ein neues Fraktal.",
  "Umbenennen",
  "Benennt das Fraktal in der Liste um.",
  "L�scht das gerade gew�hlte Fraktal der Liste.",
  "W�hlen Sie einen Farbverlauf durch Doppelklick aus",
  "F�ge FractalExplorer-Pfad hinzu",
  "Nach neuen Fraktalen suchen",
  "Neues Verzeichis hinzuf�gen.",
  "Fraktal-Optionen",
  "Parameter",
  "�ndert die erste (minimale) Begrenzung der x-Koordinate.",
  "�ndert die zweite (maximale) Begrenzung der x-Koordinate.",
  "�ndert die erste (minimale) Begrenzung der y-Koordinate.",
  "�ndert die zweite (maximale) Begrenzung der y-Koordinate.",
  "�ndert die Iterations-Variable. Je h�her sie ist, um so genauer und detailierter wird das Bild sein. Eine gr�ssere Berechenzeit ist allerdings in Kauf zu nehmen.",
  "�ndert den CX-Wert (Dies wirkt sich auf alle Fraktale aus, ausser Mandelbrot und Sierpinski).",
  "�ndert den CY-Wert (Dies wirkt sich auf alle Fraktale aus, ausser Mandelbrot und Sierpinski).",
  "Setze Parameter auf die Standardwerte zurueck.",
  "Lade ein Fraktal aus einer Datei",
  "Speichere das aktive Fraktal in eine Datei",
  "Fraktal-Typ",
  "Diverse Optionen",
  "Zur�cksetzen",
  "Vorschau-Optionen",
  "Echtzeit-Vorschau",
  "Falls Sie diese Option aktivieren, wird das Vorschaufenster stets automatisch aktualisiert.",
  "Neu zeichnen",
  "Zeichnet die Vorschau neu",
  "Zoom-Optionen",
  "Rueckg�ngig",
  "Macht den letzten Zoom-Vorgang wieder r�ckg�ngig.",
  "Wiederherstellen",
  "Stellt den letzten Zoom-Vorgang wieder her.",
  "Hinein",
  "Hinaus",
  "Farb-Optionen",
  "Farbintensit�t",
  "Rot",
  "Gruen",
  "Blau",
  "�ndert die Intensit�t des roten Kanals.",
  "�ndert die Intensit�t des gruenen Kanals.",
  "�ndert die Intensit�t des blauen Kanals.",
  "Farb-Funktion",
  "Sinus",
  "Cosinus",
  "Keine",
  "Verwende Sinus-Funktion f�r diese Farbkomponente.",
  "Verwende Cosinus-Funktion f�r diese Farbkomponente.",
  "Verwende lineare Farbabstufung statt einer trigonometrischen Funktion.",
  "Inversion",
  "Falls Sie diese Option aktivieren, werden tiefere Farbwerte durch h�here ausgetauscht und umgekehrt.",
  "Farb-Modus",
  "Wie oben stehend angegeben",
  "Berechne Farbpalette mit den oben angegebenen Optionen (Farb-Intensit�t/-Funktion). Das Resultat ist in der Vorschau sichtbar.",
  "Wende aktiven Farbverlauf an",
  "Berechne Farbpalette mit den Angaben eines Verlaufes aus dem Gradient-Editor.",
  "Farbverl�ufe",
  "Fraktale",
  "Akzeptiere Einstellungen und starte die Berechnung des Fraktals.",
  "Verwerfe jegliche �nderungen und schliesse das Fenster.",
  "�ber...",
  "Zeige Informationen �ber den Autor und das Plug-In.",
  "Info-Box schliessen",
  "Fehler beim �ffnen von '%.100s'. Konnte nicht speichern",
  "Speichern der Datei fehlgeschlagen\n",
  "Speichern: Keine Datei angegeben",
  "Speichern: Kann nicht in ein Verzeichnis speichern",
  "Lade Parameter eines Fraktals",
  "Klicken Sie hier, um das Fraktal zu laden.",
  "Klicken Sie hier, um den Ladevorgang abzubrechen.",
  "Speichere Fraktalparameter",
  "Klicken Sie hier, um das Fraktal in eine Datei zu speichern.",
  "Klicken Sie hier, um den Speicherungsvorgang abzubrechen.",
  "Sprache abspeichern",
  "Klicken Sie hier, um die gewaehlte Sprache als Standard zu definieren und in die Konfigurationsdatei abzuspeichern.",
},

};

