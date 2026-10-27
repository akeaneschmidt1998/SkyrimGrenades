// Builds FirePots.esp (ESL-flagged) with Mutagen, then reads it back and checks every record.
//
//   FP_FirePot            MISC  the item you carry, loot and craft
//   FP_FirePotProjectile  PROJ  what flies: a missile with gravity that explodes on any contact
//   FP_FirePotExplosion   EXPL  the shatter: pottery-break + ignite sounds, scorch decal, burst of
//                               fire damage, and leaves a vanilla burning patch (FireSmLongHazard)
//   FP_FirePotBurstEnch   ENCH  the burst's fire damage (vanilla fire-enchantment effect)
//   FP_RecipeFirePot      COBJ  cooking pot: 1 Fire Salts + 1 Firewood -> 2 Fire Pots
//
// Meshes: the plugin leaves the item, projectile and explosion models empty; FirePots.dll copies
// them from vanilla forms at load (see [Visuals] in FirePots.ini), so no mesh paths are guessed
// here and a custom pot model can be dropped in later with one INI line.
//
// Usage: MakePlugin <output folder>

using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

const string PluginName = "FirePots.esp";

// Fixed form IDs: FirePots.dll looks these up, so they must never move.
const uint PotId = 0x800;
const uint ProjectileId = 0x801;
const uint ExplosionId = 0x802;
const uint BurstEnchId = 0x803;
const uint RecipeId = 0x804;

// Tuning that lives in the plugin (the throw itself is tuned in FirePots.ini).
const float BurstFireDamage = 30f;  // fire damage to everyone in the blast (resist fire applies)
const float BlastRadius = 160f;     // units (~2.3 m); the arc preview draws this ring
const float BlastPhysicalDamage = 5f;

// Vanilla forms (Skyrim.esm), IDs from Mutagen.Bethesda.FormKeys.SkyrimSE.
FormKey Vanilla(uint id) => new FormKey(ModKey.FromFileName("Skyrim.esm"), id);
var L_PROJECTILE = Vanilla(0x088767);                 // collision layer used by arrows/missiles
var TRPOilLampBreak = Vanilla(0x08BBC6);              // ceramic oil lamp shattering
var TRPOilIgniteLowSD = Vanilla(0x042526);            // oil catching fire
var MAGFirebolt01ImpactSet = Vanilla(0x01C2AF);       // scorch decal
var FireSmLongHazard = Vanilla(0x03E074);             // small burning patch with its own damage
var EnchFireDamageFFContact = Vanilla(0x04605A);      // fire damage effect used by fire enchantments
var MagicDamageFire = Vanilla(0x01CEAD);
var FireSalts = Vanilla(0x03AD5E);
var Firewood01 = Vanilla(0x06F993);
var CraftingCookpot = Vanilla(0x0A5CB3);

var outDir = args.Length > 0 ? args[0] : ".";
Directory.CreateDirectory(outDir);
var outPath = Path.Combine(outDir, PluginName);

var modKey = ModKey.FromFileName(PluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Author = "akeaneschmidt1998";
mod.ModHeader.Description = "Fire Pots: throwable fire pots with wind-up and arc preview (requires FirePots.dll / SKSE).";
mod.IsSmallMaster = true;  // ESL flag: takes no load order slot

// --- Burst damage ----------------------------------------------------------------------------
var burst = new ObjectEffect(new FormKey(modKey, BurstEnchId), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_FirePotBurstEnch",
    Name = "Fire Pot Burst",
    Flags = ObjectEffect.Flag.NoAutoCalc,
    CastType = CastType.FireAndForget,
    TargetType = TargetType.Touch,
    EnchantType = ObjectEffect.EnchantTypeEnum.Enchantment,
};
var burstEffect = new Effect { Data = new EffectData { Magnitude = BurstFireDamage, Area = 0, Duration = 0 } };
burstEffect.BaseEffect.SetTo(EnchFireDamageFFContact);
burst.Effects.Add(burstEffect);
mod.ObjectEffects.Add(burst);

// --- Shatter ---------------------------------------------------------------------------------
var explosion = new Explosion(new FormKey(modKey, ExplosionId), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_FirePotExplosion",
    Name = "Fire Pot",
    Force = 30f,
    Damage = BlastPhysicalDamage,
    Radius = BlastRadius,
    ISRadius = 0f,
    VerticalOffsetMult = 0f,
    Flags = 0,
    SoundLevel = SoundLevel.Normal,
};
explosion.ObjectEffect.SetTo(burst.FormKey);
explosion.Sound1.SetTo(TRPOilLampBreak);
explosion.Sound2.SetTo(TRPOilIgniteLowSD);
explosion.ImpactDataSet.SetTo(MAGFirebolt01ImpactSet);
explosion.PlacedObject.SetTo(FireSmLongHazard);
mod.Explosions.Add(explosion);

// --- Projectile ------------------------------------------------------------------------------
// Missile + Gravity: flies on a ballistic arc and explodes on the first thing it touches.
// Speed is overwritten by FirePots.dll on every throw (wind-up); Gravity 1 = normal gravity.
var projectile = new Projectile(new FormKey(modKey, ProjectileId), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_FirePotProjectile",
    Name = "Fire Pot",
    Type = Projectile.TypeEnum.Missile,
    Flags = Projectile.Flag.Explosion,
    Gravity = 1.0f,
    Speed = 1200f,
    Range = 30000f,
    CollisionRadius = 8f,
    Lifetime = 10f,
    ImpactForce = 0f,
    SoundLevel = (uint)SoundLevel.Normal,
};
projectile.Explosion.SetTo(explosion.FormKey);
projectile.CollisionLayer.SetTo(L_PROJECTILE);
mod.Projectiles.Add(projectile);

// --- Item ------------------------------------------------------------------------------------
var pot = new MiscItem(new FormKey(modKey, PotId), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_FirePot",
    Name = "Fire Pot",
    Value = 15,
    Weight = 1.0f,
    Keywords = new() { MagicDamageFire.ToLink<IKeywordGetter>() },
};
mod.MiscItems.Add(pot);

// --- Recipe ----------------------------------------------------------------------------------
var recipe = new ConstructibleObject(new FormKey(modKey, RecipeId), SkyrimRelease.SkyrimSE)
{
    EditorID = "FP_RecipeFirePot",
    CreatedObjectCount = 2,
    Items = new(),
};
recipe.CreatedObject.SetTo(pot.FormKey);
recipe.WorkbenchKeyword.SetTo(CraftingCookpot);
ContainerEntry Ingredient(FormKey item, int count)
{
    var ci = new ContainerItem { Count = count };
    ci.Item.SetTo(item);
    return new ContainerEntry { Item = ci };
}
recipe.Items.Add(Ingredient(FireSalts, 1));
recipe.Items.Add(Ingredient(Firewood01, 1));
mod.ConstructibleObjects.Add(recipe);

mod.BeginWrite
    .ToPath(outPath)
    .WithNoLoadOrder()
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
Require(check.ModHeader.MasterReferences.Any(m => m.Master == ModKey.FromFileName("Skyrim.esm")), "Skyrim.esm is a master");

var p = check.Projectiles.Single();
Require(p.FormKey.ID == ProjectileId && p.EditorID == "FP_FirePotProjectile", "projectile id");
Require(p.Type == Projectile.TypeEnum.Missile, "projectile is a missile");
Require(p.Flags.HasFlag(Projectile.Flag.Explosion), "projectile explodes on impact");
Require(p.Gravity > 0.5f, "projectile has gravity");
Require(p.Explosion.FormKey == explosion.FormKey, "projectile -> explosion");
Require(p.CollisionLayer.FormKey == L_PROJECTILE, "projectile collision layer");

var e = check.Explosions.Single();
Require(e.FormKey.ID == ExplosionId, "explosion id");
Require(e.ObjectEffect.FormKey == burst.FormKey, "explosion -> burst enchantment");
Require(e.PlacedObject.FormKey == FireSmLongHazard, "explosion leaves a burning patch");
Require(e.Sound1.FormKey == TRPOilLampBreak && e.Sound2.FormKey == TRPOilIgniteLowSD, "explosion sounds");
Require(Math.Abs(e.Radius - BlastRadius) < 0.01f, "explosion radius");

var en = check.ObjectEffects.Single();
Require(en.FormKey.ID == BurstEnchId && en.Effects.Count == 1, "burst enchantment has one effect");
Require(en.Effects[0].BaseEffect.FormKey == EnchFireDamageFFContact, "burst uses the vanilla fire damage effect");
Require(en.Effects[0].Data != null && Math.Abs(en.Effects[0].Data!.Magnitude - BurstFireDamage) < 0.01f, "burst magnitude");

var m = check.MiscItems.Single();
Require(m.FormKey.ID == PotId && m.Name?.String == "Fire Pot", "pot item");

var c = check.ConstructibleObjects.Single();
Require(c.FormKey.ID == RecipeId && c.CreatedObject.FormKey == pot.FormKey && c.CreatedObjectCount == 2, "recipe makes 2 pots");
Require(c.WorkbenchKeyword.FormKey == CraftingCookpot, "recipe is at the cooking pot");
Require(c.Items != null && c.Items.Count == 2, "recipe has 2 ingredients");

Console.WriteLine($"Wrote and verified {outPath} ({new FileInfo(outPath).Length} bytes): " +
                  $"pot [{m.FormKey}], projectile [{p.FormKey}], explosion [{e.FormKey}], " +
                  $"burst [{en.FormKey}], recipe [{c.FormKey}]");
