mission
(t34b, sensored brushless motor, kein gyro):

- init
init, t 102, s 120, r 1
-->
 t 101: Seben
 t 102 Crawler
 s: motor umdrehungen je meter

- wait: pause t sekunden
wait, t 3

- move
move, v 3, r 3, t 3, s 3, a 3
v: Geschw., -8..8
r: Kurve, -8..8
t: Zeit sek
s: Weg meter

Die Parameter t, s, a beschränken den Step. Wenn angegeben, gilt das Minimum.

move, t 3, v -3
--> 3 Sekunden rückwärts

find
