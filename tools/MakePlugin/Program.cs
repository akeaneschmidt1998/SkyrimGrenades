// Builds FirePots.esp (ESL-flagged) with Mutagen, then reads it back and checks every record.
//
// Bombs are trigger x payload x strength; every combination is its own item:
//   triggers   0 Impact (explodes on contact), 1 Short Fuse (bounces, 1.5 s), 2 Long Fuse (4 s),
//              3 Tripwire (placed; goes off when someone comes close)
//   payloads   0 Frag, 1 Fire, 2 Oil, 3 Smoke, 4 Fear, 5 Frenzy, 6 Concussion
//   strengths  0 Mild, 1 Moderate, 2 Major (bigger radius, more damage, longer effects)
//   index = (trigger * 7 + payload) * 3 + strength   (0..83)
//
// Records (fixed IDs: FirePots.dll computes them from the index, so they must never move):
//   0x900 + index  SCRL  the bomb: a scroll (Scrolls tab; buy, sell, stash, favorite, equip to a
//                        hand). Casting it plays the hand's release animation and fires an
//                        invisible do-nothing projectile; FirePots.dll then launches the real bomb
//   0x960 + index  MGEF  that scroll's effect (does nothing) + the bomb's description
//   0xA00 + index  PROJ  the flying/placed bomb: impact = missile, fuses/tripwire = bouncing
//                        "lobber" with a timer or a proximity trigger
//   0xA60 + p*3+s  EXPL  what the payload does, per strength
//   0xA80 + p*3+s  ENCH  the payload's effect on everyone in the blast (where it has one)
//   0xAA0..        smoke cloud / oil slick hazards, their spells and effects, fear/frenzy effects,
//                  the oil-fire explosion (FirePots.dll sets it off when a fire bomb lands in oil)
//   0x8F0          the invisible do-nothing projectile
//   0xB00          the MCM quest (MCM Helper menu)
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
var FlashImod = Sk(0x07E8FD);                // alduinExplosionFlashImod
var PushImod = Sk(0x09E586);                 // MAGShoutPush01Imod
var playerRef = FormKey.Factory("000014:Skyrim.esm");

string[] TriggerNames = { "Impact", "Short Fuse", "Long Fuse", "Tripwire" };
string[] TriggerTags = { "Impact", "ShortFuse", "LongFuse", "Tripwire" };
string[] PayloadNames = { "Frag", "Fire", "Oil", "Smoke", "Fear", "Frenzy", "Concussion" };
string[] StrengthNames = { "Mild", "Moderate", "Major" };
float[] Radius = { 140f, 210f, 280f };
uint[] BaseValue = { 12, 15, 12, 12, 18, 22, 18 };  // per payload, mild impact; x strength

var outDir = args.Length > 0 ? args[0] : ".";
Directory.CreateDirectory(outDir);
var outPath = Path.Combine(outDir, PluginName);

var modKey = ModKey.FromFileName(PluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Author = "akeaneschmidt1998";
mod.ModHeader.Description = "Fire Pots: craftable bombs (impact, fuse, tripwire; frag, fire, oil, smoke, fear, frenzy, concussion) with wind-up throws (requires FirePots.dll / SKSE).";
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

Hazard MakeHazard(uint id, string edid, string name, float radius, float lifetime, FormKey spell)
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
    h.Spell.SetTo(spell);
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
var nullProjectile = new Projectile(Own(0x8F0), SkyrimRelease.SkyrimSE)
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

// Smoke: invisibility (ends when you attack), stamina loss and no stamina regen while inside.
var smokeInvis = Mgef(0xAA8, "FP_SmokeInvisibility", "Hidden in Smoke", "Hidden in the smoke.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Invisibility), Quiet | MagicEffect.Flag.NoMagnitude | MagicEffect.Flag.Recover);
var smokeChoke = Mgef(0xAA9, "FP_SmokeChoke", "Choking Smoke", "Losing stamina in the smoke.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.ValueModifier) { ActorValue = ActorValue.Stamina },
    Quiet | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoDuration);
var smokeNoRegen = Mgef(0xAAA, "FP_SmokeNoRegen", "Winded", "No stamina regeneration in the smoke.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.ValueModifier) { ActorValue = ActorValue.StaminaRate },
    Quiet | MagicEffect.Flag.Detrimental | MagicEffect.Flag.Recover);
// Oil: slows anyone in the slick.
var oilSlow = Mgef(0xAB8, "FP_OilSlow", "Oil Slick", "Slowed by the oil.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.ValueModifier) { ActorValue = ActorValue.SpeedMult },
    Quiet | MagicEffect.Flag.Detrimental | MagicEffect.Flag.Recover);
// Fear / Frenzy: magnitude = highest level affected.
var fear = Mgef(0xAC0, "FP_BombFear", "Terrified", "Flees in terror.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Demoralize), MagicEffect.Flag.Hostile | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoArea);
fear.MagicSkill = ActorValue.Illusion;
var frenzy = Mgef(0xAC1, "FP_BombFrenzy", "Frenzied", "Attacks anyone nearby.",
    new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Frenzy), MagicEffect.Flag.Hostile | MagicEffect.Flag.Detrimental | MagicEffect.Flag.NoArea);
frenzy.MagicSkill = ActorValue.Illusion;

// Oil fire: what FirePots.dll sets off when a fire bomb lands in an oil slick.
var oilFireEnch = Ench(0xAC5, "FP_OilFireEnch", "Burning Oil", Eff(EnchFireDamageFFContact, 25f, 0));
var oilFire = new Explosion(Own(0xAC4), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_OilFireExplosion",
    Name = "Burning Oil",
    Force = 0f,
    Damage = 0f,
    Radius = 250f,
    ISRadius = 0f,
    VerticalOffsetMult = 0f,
    Flags = 0,
    SoundLevel = SoundLevel.Normal,
};
oilFire.ObjectEffect.SetTo(oilFireEnch.FormKey);
oilFire.Sound1.SetTo(TRPOilIgniteLowSD);
oilFire.ImpactDataSet.SetTo(ScorchImpact);
oilFire.PlacedObject.SetTo(FireLgLong);
mod.Explosions.Add(oilFire);
var oilFireProjectile = new Projectile(Own(0xAC6), SkyrimRelease.SkyrimSE)
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

// --- Payload explosions (per strength) ---------------------------------------------------------
var explosions = new Explosion[7, 3];
for (var p = 0; p < 7; p++)
{
    for (var s = 0; s < 3; s++)
    {
        var mult = s + 1;
        var tag = $"{StrengthNames[s]}{PayloadNames[p]}";
        var e = new Explosion(Own((uint)(0xA60 + p * 3 + s)), SkyrimRelease.SkyrimSE)
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
                e.SoundLevel = SoundLevel.Normal;
                e.Sound2.SetTo(FXExplosionCatapultNear);
                e.ImpactDataSet.SetTo(ScorchImpact);
                break;
            case 1:  // Fire: fire burst + burning ground
                ench = Ench((uint)(0xA80 + p * 3 + s), $"FP_{tag}Ench", "Fire Burst", Eff(EnchFireDamageFFContact, 20f * mult, 0)).FormKey;
                e.Force = 20f;
                e.Sound2.SetTo(TRPOilIgniteLowSD);
                e.ImpactDataSet.SetTo(ScorchImpact);
                e.PlacedObject.SetTo(s == 0 ? FireSmLong : s == 1 ? FireLgShort : FireLgLong);
                break;
            case 2:  // Oil: a slick that slows (and burns when a fire bomb lands in it)
            {
                var spell = ContactSpell((uint)(0xAB4 + s), $"FP_{tag}SlickSpell", "Oil Slick", Eff(oilSlow.FormKey, 20f + 15f * s, 2));
                var slick = MakeHazard((uint)(0xAB0 + s), $"FP_{tag}Slick", "Oil Slick", 120f + 60f * s, 60f, spell.FormKey);
                e.ImpactDataSet.SetTo(ScorchImpact);  // dark stain
                e.PlacedObject.SetTo(slick.FormKey);
                break;
            }
            case 3:  // Smoke: cloud; inside: invisible, losing stamina, no stamina regen
            {
                var spell = ContactSpell((uint)(0xAA4 + s), $"FP_{tag}CloudSpell", "Smoke",
                    Eff(smokeInvis.FormKey, 0f, 2), Eff(smokeChoke.FormKey, 4f + 4f * s, 0), Eff(smokeNoRegen.FormKey, 100f, 2));
                var cloud = MakeHazard((uint)(0xAA0 + s), $"FP_{tag}Cloud", "Smoke", 200f + 100f * s, 15f + 7.5f * s, spell.FormKey);
                e.PlacedObject.SetTo(cloud.FormKey);
                break;
            }
            case 4:  // Fear: flash + everyone up to the level flees
                ench = Ench((uint)(0xA80 + p * 3 + s), $"FP_{tag}Ench", "Terror", Eff(fear.FormKey, 10f * mult, 4 + 6 * mult)).FormKey;
                e.ImageSpaceModifier.SetTo(FlashImod);
                e.ISRadius = 300f + 200f * s;
                e.Sound2.SetTo(ThunderA);
                e.ImpactDataSet.SetTo(ShockImpact);
                break;
            case 5:  // Frenzy: everyone up to the level attacks whoever is nearest
                ench = Ench((uint)(0xA80 + p * 3 + s), $"FP_{tag}Ench", "Frenzy", Eff(frenzy.FormKey, 10f * mult, 4 + 6 * mult)).FormKey;
                e.Sound2.SetTo(FXExplosionIceSD);
                break;
            case 6:  // Concussion: wide stagger, very loud
                ench = Ench((uint)(0xA80 + p * 3 + s), $"FP_{tag}Ench", "Concussion", Eff(StaggerAttackFFContact, 0.5f + 0.25f * s, 0)).FormKey;
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
string Describe(int t, int p, int s)
{
    var what = p switch
    {
        0 => "a burst of shrapnel",
        1 => "a burst of flame that leaves the ground burning",
        2 => "a slick of oil that slows anyone in it and burns when a fire bomb lands in it",
        3 => "a lingering cloud that hides anyone inside until they strike, but chokes their stamina",
        4 => $"a blinding flash that sends anyone up to level {10 * (s + 1)} fleeing",
        5 => $"a cloud that drives anyone up to level {10 * (s + 1)} into a frenzy",
        _ => "a deafening blast that staggers everyone around it",
    };
    var how = t switch
    {
        0 => "Shatters on impact into",
        1 => "Bursts a moment after landing into",
        2 => "Bursts a few seconds after landing into",
        _ => "Set it down; when someone comes close it bursts into",
    };
    return $"{how} {what}.";
}

for (var t = 0; t < 4; t++)
{
    for (var p = 0; p < 7; p++)
    {
        for (var s = 0; s < 3; s++)
        {
            var i = (uint)((t * 7 + p) * 3 + s);
            var name = $"{StrengthNames[s]} {PayloadNames[p]} Bomb ({TriggerNames[t]})";
            var tag = $"{StrengthNames[s]}{PayloadNames[p]}{TriggerTags[t]}";
            var desc = Describe(t, p, s);

            var projectile = new Projectile(Own(0xA00 + i), SkyrimRelease.SkyrimSE)
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
                projectile.Type = Projectile.TypeEnum.Missile;
                projectile.Flags = Projectile.Flag.Explosion;
            }
            else
            {
                // bouncing "lobber" that goes off on its timer (fuse) or when someone comes close (tripwire)
                projectile.Type = Projectile.TypeEnum.Lobber;
                projectile.Flags = Projectile.Flag.Explosion | Projectile.Flag.AltTrigger;
                projectile.ExplosionAltTriggerTimer = t == 1 ? 1.5f : t == 2 ? 4f : 900f;
                projectile.ExplosionAltTriggerProximity = t == 3 ? 120f : 0f;
            }
            projectile.Explosion.SetTo(explosions[p, s].FormKey);
            projectile.CollisionLayer.SetTo(L_PROJECTILE);
            mod.Projectiles.Add(projectile);

            var itemEffect = new MagicEffect(Own(0x960 + i), SkyrimRelease.SkyrimSE)
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

            var bomb = new Scroll(Own(0x900 + i), SkyrimRelease.SkyrimSE)
            {
                EditorID = $"FP_{tag}",
                Name = name,
                Description = desc,
                Value = BaseValue[p] * (uint)(s + 1) + (t == 0 ? 0u : t == 3 ? 6u : 3u),
                Weight = 1.0f,
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

// --- MCM quest (same setup as DaggerfallCombat: MCM Helper reads the menu from config.json) ----
var quest = new Quest(Own(0xB00), SkyrimRelease.SkyrimSE)
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
Require(check.Scrolls.Count() == 84, $"84 bombs (got {check.Scrolls.Count()})");
var maxId = check.EnumerateMajorRecords().Max(r => r.FormKey.ID);
Require(maxId <= 0xFFF, $"all IDs fit an ESL (max {maxId:X})");

for (var t = 0; t < 4; t++)
{
    for (var p = 0; p < 7; p++)
    {
        for (var s = 0; s < 3; s++)
        {
            var i = (uint)((t * 7 + p) * 3 + s);
            var bomb = check.Scrolls.Single(x => x.FormKey.ID == 0x900 + i);
            Require(bomb.Name?.String == $"{StrengthNames[s]} {PayloadNames[p]} Bomb ({TriggerNames[t]})", $"bomb {i:X} name");
            Require((int)bomb.Type == 13 && bomb.EquipmentType.FormKey == EitherHand, $"bomb {i:X} is an either-hand scroll");
            Require(bomb.Effects.Count == 1 && bomb.Effects[0].BaseEffect.FormKey.ID == 0x960 + i, $"bomb {i:X} effect");
            var proj = check.Projectiles.Single(x => x.FormKey.ID == 0xA00 + i);
            Require(proj.Explosion.FormKey.ID == 0xA60 + p * 3 + s, $"bomb {i:X} projectile -> explosion");
            Require(t == 0 ? proj.Type == Projectile.TypeEnum.Missile && !proj.Flags.HasFlag(Projectile.Flag.AltTrigger)
                           : proj.Type == Projectile.TypeEnum.Lobber && proj.Flags.HasFlag(Projectile.Flag.AltTrigger), $"bomb {i:X} trigger");
            if (t == 3) Require(proj.ExplosionAltTriggerProximity > 50f, $"bomb {i:X} tripwire proximity");
        }
    }
}
for (var p = 0; p < 7; p++)
{
    for (var s = 0; s < 3; s++)
    {
        var e = check.Explosions.Single(x => x.FormKey.ID == 0xA60 + p * 3 + s);
        Require(e.Radius >= Radius[s], $"{PayloadNames[p]} {StrengthNames[s]} radius");
        Require(p != 6 || e.SoundLevel == SoundLevel.VeryLoud, "concussion is very loud");
        Require(p != 3 || check.Hazards.Any(h => h.FormKey == e.PlacedObject.FormKey && h.Lifetime >= 15f), "smoke lingers");
        Require(p != 2 || check.Hazards.Any(h => h.FormKey == e.PlacedObject.FormKey), "oil leaves a slick");
    }
}
var q = check.Quests.Single();
Require(q.EditorID == "FirePots_MCM" && q.Flags.HasFlag(Quest.Flag.StartGameEnabled), "MCM quest");
Require(q.VirtualMachineAdapter?.Scripts.Single().Name == "FirePots_MCM", "MCM quest script");

Console.WriteLine($"Wrote and verified {outPath} ({new FileInfo(outPath).Length} bytes): " +
                  $"{check.Scrolls.Count()} bombs, {check.Explosions.Count()} explosions, {check.Hazards.Count()} hazards, max ID {maxId:X}");
