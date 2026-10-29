// Builds FirePots.esp (ESL-flagged) with Mutagen, then reads it back and checks every record.
//
// Bombs are trigger x payload x strength; every combination is its own item:
//   triggers   0 Impact (explodes on contact), 1 Short Fuse (2 s), 2 Long Fuse (4.5 s),
//              3 Tripwire (placed; goes off when anyone comes close - FirePots.dll watches)
//   Any bomb lying in the world (set down, dropped, or a fuse bomb still rolling) goes off when
//   struck or caught in another bomb's blast (FirePots.dll).
//   payloads   0 Frag, 1 Fire, 2 Frost, 3 Oil, (4 Smoke: out for now, IDs reserved), 5 Fear, 6 Frenzy,
//              7 Concussion
//   strengths  0 Mild, 1 Moderate, 2 Major (bigger radius, more damage, longer effects)
//   slot = (trigger * 16 + payload) * 3 + strength   (room for 8 triggers x 16 payloads, so adding
//          one never moves an existing ID; FirePots.dll computes the same numbers, src/Bombs.h)
//
// Records (fixed IDs):
//   0x800 + slot   SCRL  the bomb: a scroll (Scrolls tab; buy, sell, stash, favorite, equip to a
//                        hand). Casting it plays the hand's release animation and fires an
//                        invisible do-nothing projectile; FirePots.dll then launches the real bomb
//   0x980 + slot   MGEF  that scroll's effect (does nothing) + the bomb's description
//   0xB00 + slot   PROJ  the flying bomb: impact = missile; tripwire = "lobber" with a
//                        proximity trigger. Unused for fuses and tripwires: FirePots.dll drops
//                        the bomb item itself (thrown or set down) and watches it
//   0xC80 + p*3+s  EXPL  what the payload does, per strength
//   0xCB0 + p*3+s  ENCH  the payload's effect on everyone in the blast (where it has one)
//   0xCE0..0xCFF   smoke clouds, oil slicks, their spells and effects, fear/frenzy effects,
//                  the oil-fire explosion (FirePots.dll sets it off when a fire bomb lands in oil)
//   0xCF5          the invisible do-nothing projectile
//   0xD00          the MCM quest (MCM Helper menu)
//   0xD10..0xD5F   debug smoke bombs (hazard, explosion, projectile, effect, scroll per candidate
//                  smoke look; FirePots.dll gives each its look from FirePots.ini [Debug])
//   0xD60..0xD68   smoke's second layer (low mist): hazard, explosion, projectile per strength;
//                  FirePots.dll drops it where a smoke bomb burst
//   0xD70..0xD72   short "burst" hazards that give frost, fear and frenzy something to see
//   0xD80..0xD97   burst looks: hazard, explosion, projectile per payload (FirePots.dll drops one
//                  where a bomb bursts, so every burst shows)
//   0xDA0..0xDA3   reserved (casting art for the bomb in hand didn't show; FirePots.dll attaches it)
//   0xD74..0xD79   fire's spread: explosion + projectile per strength placing burning ground;
//                  FirePots.dll drops a ring of them around a fire burst
//   Visible looks are borrowed at load from vanilla forms by FirePots.dll ([Visuals]): hazards
//   take a borrowed model reliably, explosions apparently don't.
//
// No recipes: bombs are made anywhere with FirePots.dll's menu (craft key, H).
// Meshes: models are left empty; FirePots.dll copies them from vanilla forms at load.
//
// Usage: MakePlugin <output folder>

using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;  // ExtendedList

const string PluginName = "FirePots.esp";

var skyrim = ModKey.FromFileName("Skyrim.esm");
FormKey Sk(uint id) => new FormKey(skyrim, id);

// Vanilla forms, IDs from Mutagen.Bethesda.FormKeys.SkyrimSE.
var L_PROJECTILE = Sk(0x088767);
var EitherHand = Sk(0x013F44);
var VendorItemScroll = Sk(0x0A0E57);
var VendorItemPotion = Sk(0x08CDEC);
var TRPOilLampBreak = Sk(0x08BBC6);         // shatter
var TRPOilIgniteLowSD = Sk(0x042526);
var FXExplosionIceSD = Sk(0x023992);
var FXExplosionCatapultNear = Sk(0x0FD989);  // big boom
var ThunderA = Sk(0x0A0F52);                 // Unrelenting Force thunder
var ScorchImpact = Sk(0x01C2AF);             // MAGFirebolt01ImpactSet
var ShockImpact = Sk(0x038B05);              // MAGShock01ImpactSet
var FireSmLong = Sk(0x03E074);
var FireLgShort = Sk(0x03E076);
var FireLgLong = Sk(0x03E075);
var EnchFireDamageFFContact = Sk(0x04605A);
var StaggerAttackFFContact = Sk(0x07809B);
var EnchFrostDamageFFContact = Sk(0x04605B);
var FrostSlowFFContact = Sk(0x0B72A0);
var FrostImpact = Sk(0x018A2E);              // MAGFrost01ImpactSet
var FlashImod = Sk(0x07E8FD);                // alduinExplosionFlashImod
var PushImod = Sk(0x09E586);                 // MAGShoutPush01Imod
var playerRef = FormKey.Factory("000014:Skyrim.esm");

string[] TriggerNames = { "Impact", "Short Fuse", "Long Fuse", "Tripwire" };
string[] TriggerTags = { "Impact", "ShortFuse", "LongFuse", "Tripwire" };
const int Triggers = 4;
string[] PayloadNames = { "Frag", "Fire", "Frost", "Oil", "Smoke", "Fear", "Frenzy", "Concussion" };
bool Available(int p) => p != 4;  // smoke is out (it only made everyone in it invisible); IDs reserved
const int Payloads = 8;
uint Slot(int t, int p, int s) => (uint)((t * 16 + p) * 3 + s);
string[] StrengthNames = { "Mild", "Moderate", "Major" };
float[] Radius = { 140f, 210f, 280f };
uint[] BaseValue = { 12, 15, 15, 12, 12, 18, 22, 18 };  // per payload, mild impact; x strength

var outDir = args.Length > 0 ? args[0] : ".";
Directory.CreateDirectory(outDir);
var outPath = Path.Combine(outDir, PluginName);

var modKey = ModKey.FromFileName(PluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Author = "akeaneschmidt1998";
mod.ModHeader.Description = "Fire Pots: craftable bombs (impact, fuse, tripwire; frag, fire, frost, oil, fear, frenzy, concussion) with wind-up throws (requires FirePots.dll / SKSE).";
mod.IsSmallMaster = true;
FormKey Own(uint id) => new FormKey(modKey, id);

MagicEffect Mgef(uint id, string edid, string name, string desc, MagicEffectArchetype arch, MagicEffect.Flag flags)
{
    var m = new MagicEffect(Own(id), SkyrimRelease.SkyrimSE)
    {
        EditorID = edid,
        Name = name,
        Description = desc,
        Flags = flags,
        Archetype = arch,
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Touch,
        CastingSoundLevel = SoundLevel.Silent,
    };
    mod.MagicEffects.Add(m);
    return m;
}

Effect Eff(FormKey mgef, float magnitude, int duration)
{
    var e = new Effect { Data = new EffectData { Magnitude = magnitude, Area = 0, Duration = duration } };
    e.BaseEffect.SetTo(mgef);
    return e;
}

Spell ContactSpell(uint id, string edid, string name, params Effect[] effects)
{
    var s = new Spell(Own(id), SkyrimRelease.SkyrimSE)
    {
        EditorID = edid,
        Name = name,
        Description = name,
        Type = SpellType.Spell,
        Flags = SpellDataFlag.ManualCostCalc | SpellDataFlag.NoAbsorbOrReflect,
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Touch,
    };
    foreach (var e in effects) s.Effects.Add(e);
    mod.Spells.Add(s);
    return s;
}

Hazard MakeHazard(uint id, string edid, string name, float radius, float lifetime, FormKey? spell)
{
    var h = new Hazard(Own(id), SkyrimRelease.SkyrimSE)
    {
        EditorID = edid,
        Name = name,
        Limit = 6,
        Radius = radius,
        Lifetime = lifetime,
        TargetInterval = 1f,  // re-applies its spell every second to whoever is inside
        Flags = Hazard.Flag.DropToGround,
    };
    if (spell is { } sp) h.Spell.SetTo(sp);
    mod.Hazards.Add(h);
    return h;
}

ObjectEffect Ench(uint id, string edid, string name, params Effect[] effects)
{
    var en = new ObjectEffect(Own(id), SkyrimRelease.SkyrimSE)
    {
        EditorID = edid,
        Name = name,
        Flags = ObjectEffect.Flag.NoAutoCalc,
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Touch,
        EnchantType = ObjectEffect.EnchantTypeEnum.Enchantment,
    };
    foreach (var e in effects) en.Effects.Add(e);
    mod.ObjectEffects.Add(en);
    return en;
}

// --- Shared ---------------------------------------------------------------------------------
var nullProjectile = new Projectile(Own(0xCF5), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_NullProjectile",
    Type = Projectile.TypeEnum.Missile,
    Flags = 0,
    Gravity = 0f,
    Speed = 100f,
    Range = 10f,
    CollisionRadius = 1f,
    Lifetime = 0.05f,
    SoundLevel = (uint)SoundLevel.Silent,
};
nullProjectile.CollisionLayer.SetTo(L_PROJECTILE);
mod.Projectiles.Add(nullProjectile);

const MagicEffect.Flag Quiet = MagicEffect.Flag.Painless | MagicEffect.Flag.NoHitEvent | MagicEffect.Flag.NoArea;

// Smoke (debug bombs only for now): invisibility (ends when you attack), stamina loss and no
// stamina regen while inside.
var smokeInvis = Mgef(0xCE6, "FP_SmokeInvisibility", "Hidden in Smoke", "Hidden in the smoke.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Invisibility), Quiet | MagicEffect.Flag.NoMagnitude | MagicEffect.Flag.Recover);
var smokeChoke = Mgef(0xCE7, "FP_SmokeChoke", "Choking Smoke", "Losing stamina in the smoke.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.ValueModifier) { ActorValue = ActorValue.Stamina },
    Quiet | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoDuration);
var smokeNoRegen = Mgef(0xCE8, "FP_SmokeNoRegen", "Winded", "No stamina regeneration in the smoke.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.ValueModifier) { ActorValue = ActorValue.StaminaRate },
    Quiet | MagicEffect.Flag.Detrimental | MagicEffect.Flag.Recover);
var smokeSpells = new Spell[3];
for (var s = 0; s < 3; s++)
    smokeSpells[s] = ContactSpell((uint)(0xCE3 + s), $"FP_{StrengthNames[s]}SmokeCloudSpell", "Smoke",
        Eff(smokeInvis.FormKey, 0f, 2), Eff(smokeChoke.FormKey, 4f + 4f * s, 0), Eff(smokeNoRegen.FormKey, 100f, 2));
var smokeSpell = smokeSpells[1];  // the debug smoke bombs

// Smoke's second layer: a low mist (FirePots.dll drops it where a smoke bomb burst).
for (var s = 0; s < 3; s++)
{
    var mist = MakeHazard((uint)(0xD60 + s), $"FP_{StrengthNames[s]}SmokeMist", "Smoke", 200f + 100f * s, 15f + 7.5f * s, null);
    var mex = new Explosion(Own((uint)(0xD63 + s)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{StrengthNames[s]}SmokeMistExplosion",
        Name = "Smoke",
        Radius = 10f,
        Flags = 0,
        SoundLevel = SoundLevel.Silent,
    };
    mex.PlacedObject.SetTo(mist.FormKey);
    mod.Explosions.Add(mex);
    var mpr = new Projectile(Own((uint)(0xD66 + s)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{StrengthNames[s]}SmokeMistProjectile",
        Type = Projectile.TypeEnum.Missile,
        Flags = Projectile.Flag.Explosion,
        Gravity = 1f,
        Speed = 300f,
        Range = 1000f,
        CollisionRadius = 4f,
        Lifetime = 2f,
        SoundLevel = (uint)SoundLevel.Silent,
    };
    mpr.Explosion.SetTo(mex.FormKey);
    mpr.CollisionLayer.SetTo(L_PROJECTILE);
    mod.Projectiles.Add(mpr);
}

// Something to see when frost, fear and frenzy bombs burst (models borrowed at load).
var frostBurst = MakeHazard(0xD70, "FP_FrostBurstVisual", "Frost", 200f, 3f, null);
var fearBurst = MakeHazard(0xD71, "FP_FearBurstVisual", "Flash", 200f, 2f, null);
var frenzyBurst = MakeHazard(0xD72, "FP_FrenzyBurstVisual", "Frenzy", 200f, 3f, null);

// Fire's spread: FirePots.dll drops these in a ring around a fire burst so the burning ground
// covers the blast (vanilla burning-ground patches; no damage of their own beyond the fire).
for (var s = 0; s < 3; s++)
{
    var fex = new Explosion(Own((uint)(0xD74 + s)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{StrengthNames[s]}FireSpreadExplosion",
        Name = "Fire",
        Radius = 10f,
        Flags = 0,
        SoundLevel = SoundLevel.Silent,
    };
    fex.PlacedObject.SetTo(s == 2 ? FireLgLong : FireLgShort);
    mod.Explosions.Add(fex);
    var fpr = new Projectile(Own((uint)(0xD77 + s)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{StrengthNames[s]}FireSpreadProjectile",
        Type = Projectile.TypeEnum.Missile,
        Flags = Projectile.Flag.Explosion,
        Gravity = 1f,
        Speed = 300f,
        Range = 1000f,
        CollisionRadius = 4f,
        Lifetime = 3f,
        SoundLevel = (uint)SoundLevel.Silent,
    };
    fpr.Explosion.SetTo(fex.FormKey);
    fpr.CollisionLayer.SetTo(L_PROJECTILE);
    mod.Projectiles.Add(fpr);
}
// Oil: slows anyone in the slick.
var oilSlow = Mgef(0xCEF, "FP_OilSlow", "Oil Slick", "Slowed by the oil.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.ValueModifier) { ActorValue = ActorValue.SpeedMult },
    Quiet | MagicEffect.Flag.Detrimental | MagicEffect.Flag.Recover);
// Fear / Frenzy: magnitude = highest level affected.
var fear = Mgef(0xCF0, "FP_BombFear", "Terrified", "Flees in terror.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Demoralize), MagicEffect.Flag.Hostile | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoArea);
fear.MagicSkill = ActorValue.Illusion;
var frenzy = Mgef(0xCF1, "FP_BombFrenzy", "Frenzied", "Attacks anyone nearby.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Frenzy), MagicEffect.Flag.Hostile | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoArea);
frenzy.MagicSkill = ActorValue.Illusion;

// Oil fire: what FirePots.dll sets off when a fire bomb lands in an oil slick.
var oilFireEnch = Ench(0xCF3, "FP_OilFireEnch", "Burning Oil", Eff(EnchFireDamageFFContact, 25f, 0));
var oilFire = new Explosion(Own(0xCF2), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_OilFireExplosion",
    Name = "Burning Oil",
    Force = 0f,
    Damage = 0f,
    Radius = 250f,
    ISRadius = 0f,
    VerticalOffsetMult = 0f,
    Flags = 0,
    SoundLevel = SoundLevel.Silent,  // only concussion is meant to be heard far off
};
oilFire.ObjectEffect.SetTo(oilFireEnch.FormKey);
oilFire.Sound1.SetTo(TRPOilIgniteLowSD);
oilFire.ImpactDataSet.SetTo(ScorchImpact);
oilFire.PlacedObject.SetTo(FireLgLong);
mod.Explosions.Add(oilFire);
var oilFireProjectile = new Projectile(Own(0xCF4), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_OilFireProjectile",
    Type = Projectile.TypeEnum.Missile,
    Flags = Projectile.Flag.Explosion,
    Gravity = 1f,
    Speed = 200f,
    Range = 1000f,
    CollisionRadius = 4f,
    Lifetime = 2f,
    SoundLevel = (uint)SoundLevel.Silent,
};
oilFireProjectile.Explosion.SetTo(oilFire.FormKey);
oilFireProjectile.CollisionLayer.SetTo(L_PROJECTILE);
mod.Projectiles.Add(oilFireProjectile);


// --- Burst looks: a short-lived hazard per payload that FirePots.dll drops where a bomb bursts
// (its model borrowed at load; payloads with no look set skip it).
var burstProjectiles = new Projectile[Payloads];
for (var p = 0; p < Payloads; p++)
{
    var bh = MakeHazard((uint)(0xD80 + p), $"FP_{PayloadNames[p]}BurstLook", PayloadNames[p], 150f, 1.5f, null);
    var bx = new Explosion(Own((uint)(0xD88 + p)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{PayloadNames[p]}BurstLookExplosion",
        Name = PayloadNames[p],
        Radius = 10f,
        Flags = 0,
        SoundLevel = SoundLevel.Silent,
    };
    bx.PlacedObject.SetTo(bh.FormKey);
    mod.Explosions.Add(bx);
    var bp = new Projectile(Own((uint)(0xD90 + p)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{PayloadNames[p]}BurstLookProjectile",
        Type = Projectile.TypeEnum.Missile,
        Flags = Projectile.Flag.Explosion,
        Gravity = 1f,
        Speed = 300f,
        Range = 1000f,
        CollisionRadius = 4f,
        Lifetime = 2f,
        SoundLevel = (uint)SoundLevel.Silent,
    };
    bp.Explosion.SetTo(bx.FormKey);
    bp.CollisionLayer.SetTo(L_PROJECTILE);
    mod.Projectiles.Add(bp);
    burstProjectiles[p] = bp;
}

// --- Payload explosions (per strength) ---------------------------------------------------------
var explosions = new Explosion[Payloads, 3];
for (var p = 0; p < Payloads; p++)
{
    if (!Available(p)) continue;
    for (var s = 0; s < 3; s++)
    {
        var mult = s + 1;
        var tag = $"{StrengthNames[s]}{PayloadNames[p]}";
        var e = new Explosion(Own((uint)(0xC80 + p * 3 + s)), SkyrimRelease.SkyrimSE)
        {
            EditorID = $"FP_{tag}Explosion",
            Name = $"{StrengthNames[s]} {PayloadNames[p]} Bomb",
            Force = 0f,
            Damage = 0f,
            Radius = Radius[s],
            ISRadius = 0f,
            VerticalOffsetMult = 0f,
            Flags = 0,
            SoundLevel = SoundLevel.Silent,
        };
        e.Sound1.SetTo(TRPOilLampBreak);
        FormKey? ench = null;
        switch (p)
        {
            case 0:  // Frag: physical damage and a shove
                e.Damage = 25f * mult;
                e.Force = 40f * mult;
                e.Sound2.SetTo(FXExplosionCatapultNear);
                e.ImpactDataSet.SetTo(ScorchImpact);
                break;
            case 1:  // Fire: fire burst + burning ground
                ench = Ench((uint)(0xCB0 + p * 3 + s), $"FP_{tag}Ench", "Fire Burst", Eff(EnchFireDamageFFContact, 20f * mult, 0)).FormKey;
                e.Force = 20f;
                e.Sound2.SetTo(TRPOilIgniteLowSD);
                e.ImpactDataSet.SetTo(ScorchImpact);
                e.PlacedObject.SetTo(s == 0 ? FireSmLong : s == 1 ? FireLgShort : FireLgLong);
                break;
            case 2:  // Frost: frost burst (drains stamina) + slowed
                ench = Ench((uint)(0xCB0 + p * 3 + s), $"FP_{tag}Ench", "Frost Burst",
                    Eff(EnchFrostDamageFFContact, 20f * mult, 0), Eff(FrostSlowFFContact, 30f + 10f * s, 3 + 2 * s)).FormKey;
                e.Force = 20f;
                e.Sound2.SetTo(FXExplosionIceSD);
                e.ImpactDataSet.SetTo(FrostImpact);
                e.PlacedObject.SetTo(frostBurst.FormKey);
                break;
            case 3:  // Oil: a slick that slows (and burns when a fire bomb lands in it)
            {
                var spell = ContactSpell((uint)(0xCEC + s), $"FP_{tag}SlickSpell", "Oil Slick", Eff(oilSlow.FormKey, 20f + 15f * s, 2));
                var slick = MakeHazard((uint)(0xCE9 + s), $"FP_{tag}Slick", "Oil Slick", 120f + 60f * s, 60f, spell.FormKey);
                e.ImpactDataSet.SetTo(ScorchImpact);  // dark stain
                e.PlacedObject.SetTo(slick.FormKey);
                break;
            }
            case 4:  // Smoke: a lingering cloud (+ FirePots.dll's low mist); inside: hidden, choking
                e.PlacedObject.SetTo(MakeHazard((uint)(0xCE0 + s), $"FP_{tag}Cloud", "Smoke", 200f + 100f * s, 15f + 7.5f * s,
                    smokeSpells[s].FormKey).FormKey);
                break;
            case 5:  // Fear: flash + everyone up to the level flees
                ench = Ench((uint)(0xCB0 + p * 3 + s), $"FP_{tag}Ench", "Terror", Eff(fear.FormKey, 10f * mult, 4 + 6 * mult)).FormKey;
                e.ImageSpaceModifier.SetTo(FlashImod);
                e.ISRadius = 300f + 200f * s;
                e.Sound2.SetTo(ThunderA);
                e.ImpactDataSet.SetTo(ShockImpact);
                e.PlacedObject.SetTo(fearBurst.FormKey);
                break;
            case 6:  // Frenzy: everyone up to the level attacks whoever is nearest
                ench = Ench((uint)(0xCB0 + p * 3 + s), $"FP_{tag}Ench", "Frenzy", Eff(frenzy.FormKey, 10f * mult, 4 + 6 * mult)).FormKey;
                e.Sound2.SetTo(FXExplosionIceSD);
                e.PlacedObject.SetTo(frenzyBurst.FormKey);
                break;
            case 7:  // Concussion: wide stagger, very loud
                ench = Ench((uint)(0xCB0 + p * 3 + s), $"FP_{tag}Ench", "Concussion", Eff(StaggerAttackFFContact, 0.5f + 0.25f * s, 0)).FormKey;
                e.Radius = Radius[s] * 1.75f;
                e.Force = 60f * mult;
                e.SoundLevel = SoundLevel.VeryLoud;
                e.Sound2.SetTo(FXExplosionCatapultNear);
                e.ImageSpaceModifier.SetTo(PushImod);
                e.ISRadius = e.Radius * 1.5f;
                break;
        }
        if (ench is { } en) e.ObjectEffect.SetTo(en);
        mod.Explosions.Add(e);
        explosions[p, s] = e;
    }
}

// --- Bombs ---------------------------------------------------------------------------------
// Same text as Bombs::Describe in src/Bombs.h (keep the numbers in step).
string Meters(float units)
{
    var m = units / 70f;
    return m == MathF.Floor(m) ? $"{(int)m} m" : $"{m.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture)} m";
}
float BlastRadius(int p, int s) => p == 7 ? Radius[s] * 1.75f : Radius[s];
string Describe(int t, int p, int s)
{
    var r = Meters(BlastRadius(p, s));
    var how = t switch
    {
        0 => "Shatters on impact:",
        1 => "Bounces and rolls, then 2 seconds after the throw:",
        2 => "Bounces and rolls, then 4.5 seconds after the throw:",
        _ => "Set it down; 3 seconds later it's armed, and when anyone comes within 1.7 m:",
    };
    var what = p switch
    {
        0 => $"{25 * (s + 1)} damage within {r}, and a shove",
        1 => $"{20 * (s + 1)} fire damage within {r}, and the ground keeps burning",
        2 => $"{20 * (s + 1)} frost damage within {r}, and slowed by {30 + 10 * s}% for {3 + 2 * s} seconds",
        3 => $"an oil slick ({Meters(120f + 60f * s)} radius, 60 seconds) that slows by {20 + 15 * s}%; a fire bomb sets it ablaze",
        4 => "a lingering cloud that hides anyone inside until they strike, but chokes their stamina",
        5 => $"a blinding flash; anyone up to level {10 * (s + 1)} within {r} flees for {4 + 6 * (s + 1)} seconds",
        6 => $"anyone up to level {10 * (s + 1)} within {r} attacks whoever is nearest for {4 + 6 * (s + 1)} seconds",
        _ => $"a deafening blast that staggers everyone within {r}",
    };
    return $"{how} {what}.";
}

for (var t = 0; t < Triggers; t++)
{
    for (var p = 0; p < Payloads; p++)
    {
        if (!Available(p)) continue;
        for (var s = 0; s < 3; s++)
        {
            var i = Slot(t, p, s);
            var name = $"{StrengthNames[s]} {PayloadNames[p]} Bomb ({TriggerNames[t]})";
            var tag = $"{StrengthNames[s]}{PayloadNames[p]}{TriggerTags[t]}";
            var desc = Describe(t, p, s);

            var projectile = new Projectile(Own(0xB00 + i), SkyrimRelease.SkyrimSE)
            {
                EditorID = $"FP_{tag}Projectile",
                Name = name,
                Gravity = 1.0f,
                Speed = 600f,  // overwritten by FirePots.dll on every throw (wind-up)
                Range = 30000f,
                CollisionRadius = 8f,
                Lifetime = t == 3 ? 900f : 30f,
                ImpactForce = 0f,
                SoundLevel = (uint)SoundLevel.Silent,
            };
            if (t == 0)
            {
                // impact: flies and bursts on contact (FirePots.dll also drops it onto the spot
                // where any other bomb goes off)
                projectile.Type = Projectile.TypeEnum.Missile;
                projectile.Flags = Projectile.Flag.Explosion;
            }
            else
            {
                // bouncing "lobber" that goes off on its timer (fuse) or when someone comes close (tripwire)
                projectile.Type = Projectile.TypeEnum.Lobber;
                projectile.Flags = Projectile.Flag.Explosion | Projectile.Flag.AltTrigger;
                projectile.ExplosionAltTriggerTimer = t == 1 ? 2f : t == 2 ? 4.5f : 900f;
                projectile.ExplosionAltTriggerProximity = t == 3 ? 120f : 0f;
            }
            projectile.Explosion.SetTo(explosions[p, s].FormKey);
            projectile.CollisionLayer.SetTo(L_PROJECTILE);
            mod.Projectiles.Add(projectile);

            var itemEffect = new MagicEffect(Own(0x980 + i), SkyrimRelease.SkyrimSE)
            {
                EditorID = $"FP_{tag}Effect",
                Name = name,
                Description = desc,
                Flags = MagicEffect.Flag.HideInUI | MagicEffect.Flag.NoDuration | MagicEffect.Flag.NoMagnitude |
                        MagicEffect.Flag.NoArea | MagicEffect.Flag.NoHitEvent | MagicEffect.Flag.Painless,
                Archetype = new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Script),
                CastType = CastType.FireAndForget,
                TargetType = TargetType.Aimed,
                CastingSoundLevel = SoundLevel.Silent,
            };
            itemEffect.Projectile.SetTo(nullProjectile.FormKey);
            mod.MagicEffects.Add(itemEffect);

            var bomb = new Scroll(Own(0x800 + i), SkyrimRelease.SkyrimSE)
            {
                EditorID = $"FP_{tag}",
                Name = name,
                Description = desc,
                Value = BaseValue[p] * (uint)(s + 1) + (t == 0 ? 0u : t >= 3 ? 6u : 3u),  // FirePots.dll replaces it with the ingredients' worth
                Weight = 0.5f,
                Keywords = new ExtendedList<IFormLinkGetter<IKeywordGetter>>
                {
                    VendorItemScroll.ToLink<IKeywordGetter>(),
                    VendorItemPotion.ToLink<IKeywordGetter>(),
                },
                BaseCost = 0,
                Flags = SpellDataFlag.ManualCostCalc | SpellDataFlag.NoDualCastModification,
                Type = (SpellType)13,  // scroll
                ChargeTime = 0.3f,     // the hand comes up into its wind-up pose quickly; the throw is the release
                CastType = CastType.FireAndForget,
                TargetType = TargetType.Aimed,
            };
            bomb.EquipmentType.SetTo(EitherHand);
            bomb.Effects.Add(Eff(itemEffect.FormKey, 0f, 0));
            mod.Scrolls.Add(bomb);
        }
    }
}

// --- Debug smoke bombs: impact bombs whose burst leaves a 20 s cloud (the smoke effects above).
// Their looks come from FirePots.ini [Debug] at load, one candidate each.
const string DebugSmokeDesc = "Debug: tries one smoke look (FirePots.ini [Debug]). Shatters on impact into a 20 second cloud.";
for (var k = 0; k < 10; k++)
{
    var hz = MakeHazard((uint)(0xD10 + k), $"FP_DebugSmoke{k + 1}Cloud", "Smoke", 300f, 20f, smokeSpell.FormKey);
    var ex = new Explosion(Own((uint)(0xD20 + k)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_DebugSmoke{k + 1}Explosion",
        Name = "Smoke",
        Force = 0f,
        Damage = 0f,
        Radius = 210f,
        ISRadius = 0f,
        VerticalOffsetMult = 0f,
        Flags = 0,
        SoundLevel = SoundLevel.Silent,
    };
    ex.Sound1.SetTo(TRPOilLampBreak);
    ex.PlacedObject.SetTo(hz.FormKey);
    mod.Explosions.Add(ex);
    var pr = new Projectile(Own((uint)(0xD30 + k)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_DebugSmoke{k + 1}Projectile",
        Name = "Debug Smoke Bomb",
        Type = Projectile.TypeEnum.Missile,
        Flags = Projectile.Flag.Explosion,
        Gravity = 1.0f,
        Speed = 600f,
        Range = 30000f,
        CollisionRadius = 8f,
        Lifetime = 30f,
        SoundLevel = (uint)SoundLevel.Silent,
    };
    pr.Explosion.SetTo(ex.FormKey);
    pr.CollisionLayer.SetTo(L_PROJECTILE);
    mod.Projectiles.Add(pr);
    var fx = new MagicEffect(Own((uint)(0xD40 + k)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_DebugSmoke{k + 1}Effect",
        Name = $"Debug Smoke Bomb {k + 1}",
        Description = DebugSmokeDesc,
        Flags = MagicEffect.Flag.HideInUI | MagicEffect.Flag.NoDuration | MagicEffect.Flag.NoMagnitude |
                MagicEffect.Flag.NoArea | MagicEffect.Flag.NoHitEvent | MagicEffect.Flag.Painless,
        Archetype = new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Script),
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Aimed,
        CastingSoundLevel = SoundLevel.Silent,
    };
    fx.Projectile.SetTo(nullProjectile.FormKey);
    mod.MagicEffects.Add(fx);
    var db = new Scroll(Own((uint)(0xD50 + k)), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_DebugSmoke{k + 1}",
        Name = $"Debug Smoke Bomb {k + 1}",
        Description = DebugSmokeDesc,
        Value = 1,
        Weight = 0.1f,
        BaseCost = 0,
        Flags = SpellDataFlag.ManualCostCalc | SpellDataFlag.NoDualCastModification,
        Type = (SpellType)13,
        ChargeTime = 0.3f,
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Aimed,
    };
    db.EquipmentType.SetTo(EitherHand);
    db.Effects.Add(Eff(fx.FormKey, 0f, 0));
    mod.Scrolls.Add(db);
}

// --- MCM quest (same setup as DaggerfallCombat: MCM Helper reads the menu from config.json) ----
var quest = new Quest(Own(0xD00), SkyrimRelease.SkyrimSE)
{
    EditorID = "FirePots_MCM",
    Name = "Fire Pots MCM",
    Flags = Quest.Flag.StartGameEnabled,
    Priority = 0,
    Type = Quest.TypeEnum.None,
    NextAliasID = 1,
};
var alias = new QuestAlias { ID = 0, Type = QuestAlias.TypeEnum.Reference, Name = "PlayerAlias" };
alias.ForcedReference.SetTo(playerRef);
quest.Aliases.Add(alias);
var vmad = new QuestAdapter();
vmad.Scripts.Add(new ScriptEntry { Name = "FirePots_MCM" });
var aliasScripts = new QuestFragmentAlias { Property = new ScriptObjectProperty { Alias = 0 } };
aliasScripts.Property.Object.SetTo(quest.FormKey);
aliasScripts.Scripts.Add(new ScriptEntry { Name = "SKI_PlayerLoadGameAlias" });
vmad.Aliases.Add(aliasScripts);
quest.VirtualMachineAdapter = vmad;
mod.Quests.Add(quest);

mod.BeginWrite
    .ToPath(outPath)
    .WithLoadOrder(skyrim)
    .WithNoDataFolder()
    .Write();

// --- Verify by reading the file back -------------------------------------------------------
using var check = SkyrimMod.CreateFromBinaryOverlay(outPath, SkyrimRelease.SkyrimSE);

void Require(bool ok, string what)
{
    if (!ok)
    {
        Console.Error.WriteLine($"VERIFY FAILED: {what}");
        Environment.Exit(1);
    }
}

Require(check.IsSmallMaster, "plugin is ESL-flagged");
var masters = check.ModHeader.MasterReferences.Select(m => m.Master).ToList();
Require(masters.SequenceEqual(new[] { skyrim }), $"only master is Skyrim.esm (got {string.Join(", ", masters)})");
Require(!check.ConstructibleObjects.Any() && !check.MiscItems.Any(), "no recipes or misc items");
var available = Enumerable.Range(0, Payloads).Count(Available);
Require(check.Scrolls.Count() == Triggers * available * 3 + 10, $"{Triggers * available * 3} bombs + 10 debug (got {check.Scrolls.Count()})");
Require(check.Scrolls.All(x => x.Weight <= 0.5f), "bombs weigh 0.5");
Require(check.Explosions.Where(x => x.SoundLevel != SoundLevel.Silent).All(x => x.FormKey.ID == 0xC80 + 7 * 3 || x.FormKey.ID == 0xC80 + 7 * 3 + 1 || x.FormKey.ID == 0xC80 + 7 * 3 + 2), "only concussion makes noise");
Require(check.Hazards.Count(h => h.FormKey.ID is >= 0xD60 and <= 0xD62) == 3 && check.Hazards.Any(h => h.FormKey.ID == 0xD70), "smoke mist + burst visuals");
for (var k = 0; k < 10; k++)
{
    var db = check.Scrolls.Single(x => x.FormKey.ID == 0xD50 + k);
    var dp = check.Projectiles.Single(x => x.FormKey.ID == 0xD30 + k);
    var de = check.Explosions.Single(x => x.FormKey.ID == 0xD20 + k);
    Require(db.Effects[0].BaseEffect.FormKey.ID == 0xD40 + k && dp.Explosion.FormKey.ID == 0xD20 + k &&
            de.PlacedObject.FormKey.ID == 0xD10 + k, $"debug smoke bomb {k + 1} wiring");
}
var maxId = check.EnumerateMajorRecords().Max(r => r.FormKey.ID);
Require(maxId <= 0xFFF, $"all IDs fit an ESL (max {maxId:X})");

for (var t = 0; t < Triggers; t++)
{
    for (var p = 0; p < Payloads; p++)
    {
        if (!Available(p)) continue;
        for (var s = 0; s < 3; s++)
        {
            var i = Slot(t, p, s);
            var bomb = check.Scrolls.Single(x => x.FormKey.ID == 0x800 + i);
            Require(bomb.Name?.String == $"{StrengthNames[s]} {PayloadNames[p]} Bomb ({TriggerNames[t]})", $"bomb {i:X} name");
            Require(bomb.Description?.String == Describe(t, p, s), $"bomb {i:X} description");
            Require((int)bomb.Type == 13 && bomb.EquipmentType.FormKey == EitherHand, $"bomb {i:X} is an either-hand scroll");
            Require(bomb.Effects.Count == 1 && bomb.Effects[0].BaseEffect.FormKey.ID == 0x980 + i, $"bomb {i:X} effect");
            var proj = check.Projectiles.Single(x => x.FormKey.ID == 0xB00 + i);
            Require(proj.Explosion.FormKey.ID == 0xC80 + p * 3 + s, $"bomb {i:X} projectile -> explosion");
            Require(t == 0 ? proj.Type == Projectile.TypeEnum.Missile && !proj.Flags.HasFlag(Projectile.Flag.AltTrigger)
                           : proj.Type == Projectile.TypeEnum.Lobber && proj.Flags.HasFlag(Projectile.Flag.AltTrigger), $"bomb {i:X} trigger");
            if (t == 3) Require(proj.ExplosionAltTriggerProximity > 50f, $"bomb {i:X} tripwire proximity");
        }
    }
}
for (var p = 0; p < Payloads; p++)
{
    if (!Available(p)) continue;
    for (var s = 0; s < 3; s++)
    {
        var e = check.Explosions.Single(x => x.FormKey.ID == 0xC80 + p * 3 + s);
        Require(e.Radius >= Radius[s], $"{PayloadNames[p]} {StrengthNames[s]} radius");
        Require(p != 7 || e.SoundLevel == SoundLevel.VeryLoud, "concussion is very loud");
        Require(p != 2 || !e.ObjectEffect.IsNull, "frost has a burst");
        Require(p != 3 || check.Hazards.Any(h => h.FormKey == e.PlacedObject.FormKey), "oil leaves a slick");
        Require(p != 4 || check.Hazards.Any(h => h.FormKey == e.PlacedObject.FormKey && h.Lifetime >= 15f), "smoke lingers");
        Require(p is 0 or 7 || !e.PlacedObject.IsNull, $"{PayloadNames[p]} leaves something to see");
    }
}
var q = check.Quests.Single();
Require(q.EditorID == "FirePots_MCM" && q.Flags.HasFlag(Quest.Flag.StartGameEnabled), "MCM quest");
Require(q.VirtualMachineAdapter?.Scripts.Single().Name == "FirePots_MCM", "MCM quest script");

Console.WriteLine($"Wrote and verified {outPath} ({new FileInfo(outPath).Length} bytes): " +
                  $"{check.Scrolls.Count()} bombs, {check.Explosions.Count()} explosions, {check.Hazards.Count()} hazards, max ID {maxId:X}");
