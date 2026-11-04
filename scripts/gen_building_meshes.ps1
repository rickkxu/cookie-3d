# Generates the bakery and cookie factory as faceted low-poly OBJ files (with .mtl, one material slot per material
# instance they use), in the same flat-shaded style as the kit:
#   Bakery.obj  - pink shopfront: display windows, striped scalloped awning, Dutch-gable false front with a round
#                 window, flat chocolate roof with icing drips, chimney
#   Factory.obj - brick hall with sawtooth glass roof, rows of windows, loading door, two smokestacks, steel posts
#                 for the sign, and a massive launch chute out of the east side
# Both signs are the game's own cookie (Mesh_KitchenObjects_Cookie_01), stood on edge facing the street (-Y) and
# merged onto these shells in the editor (SceneTools.merge_actors) to make SM_BakeryShop and SM_CookieFactory:
#   bakery:  centre (0, -331, 410), scale 10.4 (70 cm radius), back flat against the wall above the awning
#   factory: centre (-480, -470, 1430), scale 67 (4.5 m radius), on the posts
# Usage: powershell -File gen_building_meshes.ps1 <output folder>
# Geometry is written in Unreal's axes (cm, X forward, Y right, Z up; the front faces -Y). The OBJ importer mirrors
# Y, so the files store -Y with reversed windings and come in exactly as written here.
# Slot names are the material instances to assign after import (see the list printed at the end).
param([string]$OutDir = ".")

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$inv = [System.Globalization.CultureInfo]::InvariantCulture
function Fmt([double]$v) { return $v.ToString("0.###", $inv) }

# ---------------------------------------------------------------------------------------------------------------
# Vector helpers (arrays of 3 doubles)
function V([double]$x, [double]$y, [double]$z) { return ,@($x, $y, $z) }
function Add($a, $b) { return ,@(($a[0]+$b[0]), ($a[1]+$b[1]), ($a[2]+$b[2])) }
function Sub($a, $b) { return ,@(($a[0]-$b[0]), ($a[1]-$b[1]), ($a[2]-$b[2])) }
function Mul($a, [double]$s) { return ,@(($a[0]*$s), ($a[1]*$s), ($a[2]*$s)) }
function Cross($a, $b) { return ,@(($a[1]*$b[2]-$a[2]*$b[1]), ($a[2]*$b[0]-$a[0]*$b[2]), ($a[0]*$b[1]-$a[1]*$b[0])) }
function Len($a) { return [Math]::Sqrt($a[0]*$a[0]+$a[1]*$a[1]+$a[2]*$a[2]) }
function Unit($a) { $l = Len $a; if ($l -lt 1e-9) { return ,@(0.0,0.0,1.0) }; return Mul $a (1.0/$l) }

# Two unit vectors u, v with u x v = Axis.
function Basis($Axis) {
    $a = Unit $Axis
    $ref = if ([Math]::Abs($a[2]) -lt 0.9) { V 0 0 1 } else { V 1 0 0 }
    $u = Unit (Cross $ref $a)
    $v = Cross $a $u
    return ,@($u, $v)
}

# ---------------------------------------------------------------------------------------------------------------
# Mesh being built: polygons grouped by material. Each polygon is a list of points, counter-clockwise seen from
# outside (right-hand rule gives the outward normal).
$script:Groups = [ordered]@{}
function Begin-Mesh { $script:Groups = [ordered]@{} }
function Poly([string]$Mat, $Pts) {
    if (-not $script:Groups.Contains($Mat)) { $script:Groups[$Mat] = New-Object System.Collections.ArrayList }
    [void]$script:Groups[$Mat].Add($Pts)
}

# Box from half-extent vectors Ex, Ey, Ez (a right-handed set) around C.
function BoxB([string]$Mat, $C, $Ex, $Ey, $Ez) {
    function P($sx, $sy, $sz) { return Add (Add (Add $C (Mul $Ex $sx)) (Mul $Ey $sy)) (Mul $Ez $sz) }
    Poly $Mat @((P 1 -1 -1), (P 1 1 -1), (P 1 1 1), (P 1 -1 1))
    Poly $Mat @((P -1 -1 -1), (P -1 -1 1), (P -1 1 1), (P -1 1 -1))
    Poly $Mat @((P -1 1 -1), (P -1 1 1), (P 1 1 1), (P 1 1 -1))
    Poly $Mat @((P -1 -1 -1), (P 1 -1 -1), (P 1 -1 1), (P -1 -1 1))
    Poly $Mat @((P -1 -1 1), (P 1 -1 1), (P 1 1 1), (P -1 1 1))
    Poly $Mat @((P -1 -1 -1), (P -1 1 -1), (P 1 1 -1), (P 1 -1 -1))
}

# Axis-aligned box between two corners.
function Box([string]$Mat, [double]$x0, [double]$y0, [double]$z0, [double]$x1, [double]$y1, [double]$z1) {
    BoxB $Mat (V (($x0+$x1)/2) (($y0+$y1)/2) (($z0+$z1)/2)) (V (($x1-$x0)/2) 0 0) (V 0 (($y1-$y0)/2) 0) (V 0 0 (($z1-$z0)/2))
}

# Square beam of width W from P0 to P1.
function Beam([string]$Mat, $P0, $P1, [double]$W) {
    $axis = Sub $P1 $P0
    $b = Basis $axis
    BoxB $Mat (Mul (Add $P0 $P1) 0.5) (Mul $b[0] ($W/2)) (Mul $b[1] ($W/2)) (Mul $axis 0.5)
}

# Frustum (or cylinder, or cone) along Axis from Base, Length long, radius R0 at the base and R1 at the end.
# CapEnd/CapBase: material for the end discs, or "" for open.
function Frustum([string]$Mat, $Base, $Axis, [double]$Length, [double]$R0, [double]$R1, [int]$Sides, [string]$CapBase = "", [string]$CapEnd = "", [double]$Phase = 0.0) {
    $a = Unit $Axis
    $b = Basis $a
    $endC = Add $Base (Mul $a $Length)
    $ring0 = @(); $ring1 = @()
    for ($i = 0; $i -lt $Sides; $i++) {
        $t = $Phase + 2 * [Math]::PI * $i / $Sides
        $d = Add (Mul $b[0] ([Math]::Cos($t))) (Mul $b[1] ([Math]::Sin($t)))
        $ring0 += ,(Add $Base (Mul $d $R0))
        $ring1 += ,(Add $endC (Mul $d $R1))
    }
    for ($i = 0; $i -lt $Sides; $i++) {
        $j = ($i + 1) % $Sides
        if ($R1 -lt 0.01) { Poly $Mat @($ring0[$i], $ring0[$j], $endC) }
        else { Poly $Mat @($ring0[$i], $ring0[$j], $ring1[$j], $ring1[$i]) }
    }
    if ($CapEnd -ne "" -and $R1 -ge 0.01) { Poly $CapEnd $ring1 }
    if ($CapBase -ne "") { $rev = @($ring0); [array]::Reverse($rev); Poly $CapBase $rev }
}

# Faceted sphere (flattened by Squash on Z).
function Sphere([string]$Mat, $C, [double]$R, [int]$Lon = 10, [int]$Lat = 6, [double]$Squash = 1.0) {
    $rings = @()
    for ($j = 0; $j -le $Lat; $j++) {
        $phi = [Math]::PI * $j / $Lat - [Math]::PI / 2
        $ring = @()
        for ($i = 0; $i -lt $Lon; $i++) {
            $t = 2 * [Math]::PI * $i / $Lon
            $ring += ,(Add $C (V ($R*[Math]::Cos($phi)*[Math]::Cos($t)) ($R*[Math]::Cos($phi)*[Math]::Sin($t)) ($R*$Squash*[Math]::Sin($phi))))
        }
        $rings += ,$ring
    }
    for ($j = 0; $j -lt $Lat; $j++) {
        for ($i = 0; $i -lt $Lon; $i++) {
            $k = ($i + 1) % $Lon
            if ($j -eq 0) { Poly $Mat @($rings[0][$i], $rings[1][$k], $rings[1][$i]) }
            elseif ($j -eq $Lat - 1) { Poly $Mat @($rings[$j][$i], $rings[$j][$k], $rings[$j+1][$i]) }
            else { Poly $Mat @($rings[$j][$i], $rings[$j][$k], $rings[$j+1][$k], $rings[$j+1][$i]) }
        }
    }
}

# Extrudes a convex outline (2D points, counter-clockwise in the U,V plane) from Origin along W = U x V by Depth.
function Extrude([string]$Mat, $Pts2, $Origin, $U, $Vd, [double]$Depth) {
    $W = Cross $U $Vd
    $front = @(); $back = @()
    foreach ($p in $Pts2) {
        $q = Add (Add $Origin (Mul $U $p[0])) (Mul $Vd $p[1])
        $front += ,$q
        $back += ,(Add $q (Mul $W $Depth))
    }
    Poly $Mat $back
    $rev = @($front); [array]::Reverse($rev); Poly $Mat $rev
    for ($i = 0; $i -lt $front.Count; $i++) {
        $j = ($i + 1) % $front.Count
        Poly $Mat @($front[$i], $front[$j], $back[$j], $back[$i])
    }
}

# ---------------------------------------------------------------------------------------------------------------
# OBJ writer. Every polygon gets its own normal (flat shading) and planar UVs from its dominant axis (2 m per tile).
function Write-Mesh([string]$Name) {
    $obj = New-Object System.Text.StringBuilder
    $mtl = New-Object System.Text.StringBuilder
    [void]$obj.AppendLine("# generated by gen_building_meshes.ps1")
    [void]$obj.AppendLine("mtllib $Name.mtl")
    $vi = 0; $faces = 0
    foreach ($mat in $script:Groups.Keys) {
        [void]$mtl.AppendLine("newmtl $mat"); [void]$mtl.AppendLine("Kd 0.8 0.8 0.8")
        [void]$obj.AppendLine("usemtl $mat")
        foreach ($pts in $script:Groups[$mat]) {
            # Newell normal (robust for any planar polygon).
            $nx = 0.0; $ny = 0.0; $nz = 0.0
            for ($i = 0; $i -lt $pts.Count; $i++) {
                $p = $pts[$i]; $q = $pts[($i + 1) % $pts.Count]
                $nx += ($p[1] - $q[1]) * ($p[2] + $q[2])
                $ny += ($p[2] - $q[2]) * ($p[0] + $q[0])
                $nz += ($p[0] - $q[0]) * ($p[1] + $q[1])
            }
            $l = [Math]::Sqrt($nx*$nx + $ny*$ny + $nz*$nz); if ($l -lt 1e-9) { continue }
            $nx /= $l; $ny /= $l; $nz /= $l
            $ax = [Math]::Abs($nx); $ay = [Math]::Abs($ny); $az = [Math]::Abs($nz)
            $idx = @()
            foreach ($p in $pts) {
                if ($az -ge $ax -and $az -ge $ay) { $u = $p[0]; $v = $p[1] }
                elseif ($ax -ge $ay) { $u = $p[1]; $v = $p[2] }
                else { $u = $p[0]; $v = $p[2] }
                [void]$obj.AppendLine("v $(Fmt $p[0]) $(Fmt (-$p[1])) $(Fmt $p[2])")
                [void]$obj.AppendLine("vt $(Fmt ($u/200)) $(Fmt ($v/200))")
                $vi++
                $idx += $vi
            }
            $faces++
            [void]$obj.AppendLine("vn $(Fmt $nx) $(Fmt (-$ny)) $(Fmt $nz)")
            [array]::Reverse($idx)
            [void]$obj.AppendLine("f " + (($idx | ForEach-Object { "$_/$_/$faces" }) -join " "))
        }
    }
    Set-Content -Path (Join-Path $OutDir "$Name.obj") -Value $obj.ToString() -Encoding ASCII
    Set-Content -Path (Join-Path $OutDir "$Name.mtl") -Value $mtl.ToString() -Encoding ASCII
    Write-Host "$Name : $faces polygons, materials: $($script:Groups.Keys -join ', ')"
}

$AxisX = V 1 0 0; $AxisY = V 0 1 0; $AxisZ = V 0 0 1; $NegY = V 0 -1 0

# ---------------------------------------------------------------------------------------------------------------
# Bakery: 9 m x 6.6 m shop, front facing -Y, ground at z = 0.
Begin-Mesh
$Pink = "MI_Item_BakeryPink"; $White = "MI_House_White"; $Red = "MI_Item_AwningRed"; $Choc = "MI_Item_Chocolate"
$Icing = "MI_Item_Icing"; $Glass = "MI_Item_Glass"; $Dark = "MI_Item_Interior"; $Wood = "MI_House_DarkWood"
$Stone = "MI_House_Stone"; $Brick = "MI_House_Brick"; $Metal = "MI_Item_Metal"

Box $Stone -460 -360 -10 460 345 20
Box $Pink -440 -330 20 440 330 440

# Shopfront: wooden kick panels, two big display windows, a glazed door with a step.
Box $Wood -440 -338 20 -80 -330 70
Box $Wood 80 -338 20 440 -330 70
foreach ($sx in @(-1, 1)) {
    $x0 = 105 * $sx; $x1 = 405 * $sx; if ($sx -lt 0) { $t = $x0; $x0 = $x1; $x1 = $t }
    Box $White $x0 -336 75 $x1 -330 305
    Box $Glass ($x0 + 12) -339 87 ($x1 - 12) -336 293
    $xm = ($x0 + $x1) / 2
    Box $White ($xm - 4) -342 87 ($xm + 4) -336 293
    Box $White ($x0 + 12) -342 196 ($x1 - 12) -336 202
    Box $White ($x0 - 10) -352 66 ($x1 + 10) -330 78
}
Box $White -80 -336 20 80 -330 300
Box $Wood -65 -340 22 65 -336 285
Box $Glass -45 -343 140 45 -340 265
Box $Metal 47 -350 118 55 -340 136
Box $Stone -115 -405 -10 115 -330 28

# Striped awning sloping out over the windows, with a scalloped valance.
$stripes = 14; $aw = 840.0 / $stripes
$slope = Unit (V 0 140 65); $slopeHalf = (Len (V 0 140 65)) / 2
$awNormal = Cross $AxisX $slope
for ($i = 0; $i -lt $stripes; $i++) {
    $mat = if ($i % 2 -eq 0) { $Red } else { $White }
    $cx = -420 + $aw * ($i + 0.5)
    BoxB $mat (V $cx -400 302.5) (V ($aw/2) 0 0) (Mul $slope $slopeHalf) (Mul $awNormal 2)
    Extrude $mat @(@(($cx - $aw/2), 245), @($cx, 228), @(($cx + $aw/2), 245), @(($cx + $aw/2), 272), @(($cx - $aw/2), 272)) (V 0 -468 0) $AxisX $AxisZ 3
}

# (The sign above the awning, a big cookie, is merged on in the editor: see the top of this file.)

# Dutch-gable false front with a round window, white coping.
foreach ($piece in @(
        @(@(-440, 440), @(440, 440), @(440, 490), @(-440, 490)),
        @(@(-250, 490), @(250, 490), @(250, 540), @(-250, 540)),
        @(@(-250, 540), @(250, 540), @(150, 600), @(-150, 600)))) {
    Extrude $Pink $piece (V 0 -300 0) $AxisX $AxisZ 36
}
Box $White -446 -342 490 -250 -296 500
Box $White 250 -342 490 446 -296 500
Box $White -160 -342 600 160 -296 610
Frustum $White (V 0 -336 525) $NegY 6 56 56 20 "" $White
Frustum $Glass (V 0 -342 525) $NegY 3 44 44 20 "" $Glass
Box $White -46 -346 522 46 -342 528
Box $White -3 -346 481 3 -342 569

# Flat chocolate roof behind low parapets, icing dripping down the sides and back.
Box $Choc -425 -300 440 425 315 452
Box $Pink -440 -300 440 -425 330 470
Box $Pink 425 -300 440 440 330 470
Box $Pink -440 315 440 440 330 470
$rng = New-Object System.Random(77)
Box $Icing -446 -300 418 -440 336 472
Box $Icing 440 -300 418 446 336 472
Box $Icing -446 330 418 446 336 472
for ($y = -280; $y -le 300; $y += 38) {
    foreach ($sx in @(-1, 1)) {
        $len = 18 + 40 * $rng.NextDouble()
        if ($sx -lt 0) { Box $Icing -446 ($y - 10) (418 - $len) -440 ($y + 10) 418 } else { Box $Icing 440 ($y - 10) (418 - $len) 446 ($y + 10) 418 }
        Frustum $Icing (V (443 * $sx) $y (418 - $len)) (V 0 0 -1) 8 10 2 6
    }
}
for ($x = -420; $x -le 420; $x += 38) {
    $len = 18 + 40 * $rng.NextDouble()
    Box $Icing ($x - 10) 330 (418 - $len) ($x + 10) 336 418
}

# Side windows with sills and flower boxes.
foreach ($sx in @(-1, 1)) {
    foreach ($wy in @(-170, 110)) {
        if ($sx -gt 0) {
            Box $White 440 ($wy - 70) 150 446 ($wy + 70) 330; Box $Glass 446 ($wy - 58) 162 449 ($wy + 58) 318
            Box $White 446 ($wy - 3) 162 451 ($wy + 3) 318; Box $Wood 440 ($wy - 78) 112 472 ($wy + 78) 146
        } else {
            Box $White -446 ($wy - 70) 150 -440 ($wy + 70) 330; Box $Glass -449 ($wy - 58) 162 -446 ($wy + 58) 318
            Box $White -451 ($wy - 3) 162 -446 ($wy + 3) 318; Box $Wood -472 ($wy - 78) 112 -440 ($wy + 78) 146
        }
        for ($k = -2; $k -le 2; $k++) {
            $mat = if (($k + 2) % 2 -eq 0) { $Red } else { $Pink }
            Sphere $mat (V (456 * $sx) ($wy + 30 * $k) 150) 15 8 4 0.8
        }
    }
}

# The oven's chimney at the back (just for looks: every cookie comes out of the front door).
Box $Brick -330 140 440 -230 240 680
Box $Stone -342 128 680 -218 252 700
Box $Dark -315 155 699 -245 225 702
Write-Mesh "Bakery"

# ---------------------------------------------------------------------------------------------------------------
# Cookie factory: 20 m x 12 m brick hall, front facing -Y.
Begin-Mesh
$Yellow = "MI_Item_FactoryYellow"; $Steel = "MI_House_Steel"; $Path = "MI_House_Path"

Box $Stone -1020 -620 -10 1020 620 30
Box $Brick -1000 -600 30 1000 600 700
Box $White -1010 -610 700 1010 610 730

# Sawtooth roof: four bays, steep glazed faces looking west.
for ($i = 0; $i -lt 4; $i++) {
    $x0 = -1000 + 500 * $i
    Extrude $Metal @(@($x0, 730), @(($x0 + 500), 730), @($x0, 960)) (V 0 610 0) $AxisX $AxisZ 1220
    Box $Glass ($x0 - 3) -590 745 $x0 590 945
    for ($y = -450; $y -le 450; $y += 150) { Box $White ($x0 - 6) ($y - 5) 745 ($x0 - 3) ($y + 5) 945 }
}

# Windows: two rows on every wall, a frame, glass and a cross of mullions each.
function FactoryWindow([string]$Wall, [double]$c, [double]$z0, [double]$z1) {
    $zm = ($z0 + $z1) / 2
    switch ($Wall) {
        "S" { Box $White ($c-75) -606 $z0 ($c+75) -600 $z1; Box $Glass ($c-65) -609 ($z0+10) ($c+65) -606 ($z1-10)
              Box $White ($c-3) -611 ($z0+10) ($c+3) -609 ($z1-10); Box $White ($c-65) -611 ($zm-3) ($c+65) -609 ($zm+3)
              Box $Stone ($c-85) -618 ($z0-12) ($c+85) -600 $z0 }
        "N" { Box $White ($c-75) 600 $z0 ($c+75) 606 $z1; Box $Glass ($c-65) 606 ($z0+10) ($c+65) 609 ($z1-10)
              Box $White ($c-3) 609 ($z0+10) ($c+3) 611 ($z1-10); Box $White ($c-65) 609 ($zm-3) ($c+65) 611 ($zm+3)
              Box $Stone ($c-85) 600 ($z0-12) ($c+85) 618 $z0 }
        "E" { Box $White 1000 ($c-75) $z0 1006 ($c+75) $z1; Box $Glass 1006 ($c-65) ($z0+10) 1009 ($c+65) ($z1-10)
              Box $White 1009 ($c-3) ($z0+10) 1011 ($c+3) ($z1-10); Box $White 1009 ($c-65) ($zm-3) 1011 ($c+65) ($zm+3)
              Box $Stone 1000 ($c-85) ($z0-12) 1018 ($c+85) $z0 }
        "W" { Box $White -1006 ($c-75) $z0 -1000 ($c+75) $z1; Box $Glass -1009 ($c-65) ($z0+10) -1006 ($c+65) ($z1-10)
              Box $White -1011 ($c-3) ($z0+10) -1009 ($c+3) ($z1-10); Box $White -1011 ($c-65) ($zm-3) -1009 ($c+65) ($zm+3)
              Box $Stone -1018 ($c-85) ($z0-12) -1000 ($c+85) $z0 }
    }
}
foreach ($c in @(-875, -625, -375, -125, 125, 375, 625, 875)) {
    FactoryWindow "S" $c 420 600
    FactoryWindow "N" $c 420 600
    FactoryWindow "N" $c 150 330
    if ([Math]::Abs($c) -gt 200) { FactoryWindow "S" $c 150 330 }
}
foreach ($c in @(-375, -125, 125, 375)) {
    foreach ($w in @("E", "W")) { FactoryWindow $w $c 150 330; FactoryWindow $w $c 420 600 }
}

# Loading door: ribbed steel shutter in a yellow frame, ramp in front.
Box $Yellow -165 -616 30 -130 -600 475
Box $Yellow 130 -616 30 165 -600 475
Box $Yellow -165 -616 440 165 -600 475
Box $Steel -130 -605 30 130 -600 440
for ($z = 45; $z -lt 440; $z += 32) { Box $Metal -130 -608 $z 130 -605 ($z + 6) }
Box $Path -180 -720 -10 180 -600 30

# Smokestacks (just for looks: the cookies come out of the chute).
foreach ($sx in @(150, 450)) {
    Box $Brick ($sx - 100) 230 730 ($sx + 100) 430 900
    Frustum $Brick (V $sx 330 900) $AxisZ 800 80 64 16
    Frustum $White (V $sx 330 1450) $AxisZ 26 76 75 16
    Frustum $White (V $sx 330 1520) $AxisZ 26 75 74 16
    Frustum $Metal (V $sx 330 1690) $AxisZ 34 74 74 16 $Metal $Dark
}

# Steel posts for the sign, a humongous cookie merged on in the editor (see the top of this file).
foreach ($px in @(-720, -240)) { Box $Steel ($px - 16) -486 730 ($px + 16) -454 1120 }
Box $Steel -720 -482 950 -240 -458 980

# The launch chute: a massive yellow tube out of a machine house on the roof, angled up over the east side,
# flaring at the mouth, propped on a steel A-frame. Cookies shoot out of the mouth high into the air.
$d = Unit (V 0.53 0 0.848)
$up = Cross $d $AxisY
$p0 = V 780 0 560
Box $Metal 640 -260 730 1000 260 1010
Box $Yellow 630 -270 1010 1010 270 1030
Frustum $Yellow $p0 $d 1250 160 160 20
foreach ($s in @(450, 750, 1050)) { Frustum $Steel (Add $p0 (Mul $d $s)) $d 34 172 172 20 }
$mouth = Add $p0 (Mul $d 1250)
Frustum $Yellow $mouth $d 150 160 235 20 "" $Dark
Frustum $Steel (Add $mouth (Mul $d 150)) $d 14 240 240 20 "" ""
$prop = Add (Add $p0 (Mul $d 1000)) (Mul $up -150)
foreach ($sy in @(-1, 1)) {
    Beam $Steel (V $prop[0] (270 * $sy) 30) (V $prop[0] (110 * $sy) $prop[2]) 36
    Box $Stone ($prop[0] - 45) (270 * $sy - 45) -10 ($prop[0] + 45) (270 * $sy + 45) 40
}
Beam $Steel (V $prop[0] -230 520) (V $prop[0] 230 520) 26
Beam $Steel (V $prop[0] -200 140) (V $prop[0] 140 900) 20
Write-Mesh "Factory"
$m = Add $mouth (Mul $d 170)
Write-Host ("Factory chute mouth (spout start): {0:0} {1:0} {2:0}" -f $m[0], $m[1], $m[2])

# ---------------------------------------------------------------------------------------------------------------
# Cookie cloud: faceted puffs (jittered icospheres, like the trees' leaf clumps) with a lumpy, rounded belly, white
# on top and blue-grey underneath. About 50 m x 26 m x 20 m.
# The cookies studding it are the game's own cookie, merged on in the editor at the spots this prints
# (and writes to Cloud_cookies.txt): x y z pitch yaw scale, each half sunk into a puff.
function New-IcoSphere {
    $t = (1 + [Math]::Sqrt(5)) / 2
    $v = New-Object System.Collections.ArrayList
    foreach ($p in @(@(-1,$t,0),@(1,$t,0),@(-1,-$t,0),@(1,-$t,0),@(0,-1,$t),@(0,1,$t),@(0,-1,-$t),@(0,1,-$t),@($t,0,-1),@($t,0,1),@(-$t,0,-1),@(-$t,0,1))) {
        $l = [Math]::Sqrt($p[0]*$p[0]+$p[1]*$p[1]+$p[2]*$p[2]); [void]$v.Add(@(($p[0]/$l),($p[1]/$l),($p[2]/$l)))
    }
    $f = @(@(0,11,5),@(0,5,1),@(0,1,7),@(0,7,10),@(0,10,11),@(1,5,9),@(5,11,4),@(11,10,2),@(10,7,6),@(7,1,8),
           @(3,9,4),@(3,4,2),@(3,2,6),@(3,6,8),@(3,8,9),@(4,9,5),@(2,4,11),@(6,2,10),@(8,6,7),@(9,8,1))
    $cache = @{}
    $mid = {
        param($i, $j)
        $key = if ($i -lt $j) { "$i-$j" } else { "$j-$i" }
        if ($cache.ContainsKey($key)) { return $cache[$key] }
        $a = $v[$i]; $b = $v[$j]
        $m = @((($a[0]+$b[0])/2),(($a[1]+$b[1])/2),(($a[2]+$b[2])/2))
        $l = [Math]::Sqrt($m[0]*$m[0]+$m[1]*$m[1]+$m[2]*$m[2])
        [void]$v.Add(@(($m[0]/$l),($m[1]/$l),($m[2]/$l)))
        $cache[$key] = $v.Count - 1
        return $v.Count - 1
    }
    # Subdivided twice: 320 faces, round enough to read as a puff while staying faceted.
    $faces = $f
    for ($pass = 0; $pass -lt 2; $pass++) {
        $next = New-Object System.Collections.ArrayList
        foreach ($tri in $faces) {
            $a = & $mid $tri[0] $tri[1]; $b = & $mid $tri[1] $tri[2]; $c = & $mid $tri[2] $tri[0]
            [void]$next.Add(@($tri[0],$a,$c)); [void]$next.Add(@($tri[1],$b,$a)); [void]$next.Add(@($tri[2],$c,$b)); [void]$next.Add(@($a,$b,$c))
        }
        $faces = $next
    }
    return ,@($v, $faces)
}

# A point on a puff's surface in direction Dir: the underside is a little squashed but still round, so the cloud's
# belly is lumpy puffs, not a flat base.
$clSquash = 0.7
function PuffPoint($Puff, $Dir, [double]$Scale) {
    $dz = if ($Dir[2] -lt 0) { $Dir[2] * $clSquash } else { $Dir[2] }
    return ,(V ($Puff[0] + $Dir[0] * $Puff[3] * $Scale) ($Puff[1] + $Dir[1] * $Puff[3] * $Scale) ($Puff[2] + $dz * $Puff[3] * $Scale))
}

Begin-Mesh
$CloudTop = "MI_Item_Cloud"; $CloudShade = "MI_Item_CloudShade"
# The whole cloud is drawn at this scale (cookies included).
$clS = 2.0
# x, y, z (centre), radius, before scaling. The lowest puffs hang down in the middle so the belly bulges.
$clPuffs = @(
    @(0, 0, 180, 440), @(-640, 60, 150, 370), @(640, -40, 150, 380), @(-1020, -20, 105, 260), @(1020, 40, 110, 270),
    @(-300, -340, 120, 300), @(320, 340, 125, 310), @(-290, 330, 115, 290), @(300, -330, 115, 290),
    @(-180, 0, 470, 320), @(260, 40, 440, 300), @(-650, 40, 350, 240), @(650, -20, 340, 250), @(40, -120, 640, 220), @(-420, -80, 560, 200),
    @(-330, 40, 40, 250), @(360, -30, 50, 240), @(-820, -60, 60, 190), @(840, 60, 60, 190)) | ForEach-Object { ,@(($_[0] * $clS), ($_[1] * $clS), ($_[2] * $clS), ($_[3] * $clS)) }
$clIco = New-IcoSphere
$clRng = New-Object System.Random(2024)
foreach ($clPuff in $clPuffs) {
    $clVerts = @()
    foreach ($clDir in $clIco[0]) { $clVerts += ,(PuffPoint $clPuff $clDir (0.95 + 0.1 * $clRng.NextDouble())) }
    foreach ($clFace in $clIco[1]) {
        $ca = $clVerts[$clFace[0]]; $cb = $clVerts[$clFace[1]]; $cc = $clVerts[$clFace[2]]
        $cn = Cross (Sub $cb $ca) (Sub $cc $ca)
        if ((Len $cn) -lt 1e-6) { continue }
        $mat = if ((Unit $cn)[2] -lt -0.35) { $CloudShade } else { $CloudTop }
        Poly $mat @($ca, $cb, $cc)
    }
}
Write-Mesh "Cloud"
$clMinZ = 1e9
foreach ($clPuff in $clPuffs) { $clMinZ = [Math]::Min($clMinZ, $clPuff[2] - $clSquash * $clPuff[3]) }
Write-Host ("Cloud underside lowest point: z = {0:0}" -f $clMinZ)

# Cookie spots: on the visible surface of a puff (not inside another), spaced apart, half sunk in.
# The first 28 anywhere, then 10 on the underside so the belly has cookies to see from below.
$clCookies = New-Object System.Collections.ArrayList
$clPick = New-Object System.Random(99)
$clTries = 0
while ($clCookies.Count -lt 38 -and $clTries -lt 8000) {
    $clTries++
    $clPuff = $clPuffs[$clPick.Next($clPuffs.Count)]
    $cu = 2 * $clPick.NextDouble() - 1
    if ($clCookies.Count -ge 28) { $cu = -0.35 - 0.65 * $clPick.NextDouble() }
    $cang = 2 * [Math]::PI * $clPick.NextDouble(); $cs = [Math]::Sqrt(1 - $cu * $cu)
    $clDir = V ($cs * [Math]::Cos($cang)) ($cs * [Math]::Sin($cang)) $cu
    $cp = PuffPoint $clPuff $clDir 0.97
    $hidden = $false
    foreach ($other in $clPuffs) {
        if ([object]::ReferenceEquals($other, $clPuff)) { continue }
        # Inside the other puff's (bottom-squashed) shape?
        $ox = $cp[0] - $other[0]; $oy = $cp[1] - $other[1]; $oz = $cp[2] - $other[2]
        if ($oz -lt 0) { $oz = $oz / $clSquash }
        if (($ox * $ox + $oy * $oy + $oz * $oz) -lt ($other[3] * 0.95) * ($other[3] * 0.95)) { $hidden = $true; break }
    }
    if ($hidden) { continue }
    $crowded = $false
    foreach ($prev in $clCookies) { if ((Len (Sub $cp (V $prev[6] $prev[7] $prev[8]))) -lt 230 * $clS) { $crowded = $true; break } }
    if ($crowded) { continue }
    # Normal of the squashed underside: z stretched by 1 / squash.
    $cnz = if ($clDir[2] -lt 0) { $clDir[2] / $clSquash } else { $clDir[2] }
    $cnorm = Unit (V $clDir[0] $clDir[1] $cnz)
    $cscale = (8.5 + 4.0 * $clPick.NextDouble()) * $clS
    # The game cookie is 2.74 cm thick (x scale), its pivot on its back face: sink ~45% of it.
    $cpivot = Sub $cp (Mul $cnorm (2.74 * $cscale * 0.45))
    $cpitch = [Math]::Acos([Math]::Max(-1.0, [Math]::Min(1.0, [double]$cnorm[2]))) * 180 / [Math]::PI
    $cyaw = [Math]::Atan2(-$cnorm[1], -$cnorm[0]) * 180 / [Math]::PI
    [void]$clCookies.Add(@($cpivot[0], $cpivot[1], $cpivot[2], $cpitch, $cyaw, $cscale, $cp[0], $cp[1], $cp[2]))
}
$clLines = $clCookies | ForEach-Object { "{0:0.#} {1:0.#} {2:0.#} {3:0.##} {4:0.##} {5:0.##}" -f $_[0], $_[1], $_[2], $_[3], $_[4], $_[5] }
Set-Content -Path (Join-Path $OutDir "Cloud_cookies.txt") -Value ($clLines -join "`n") -Encoding ASCII
Write-Host "Cloud cookies: $($clCookies.Count)"
