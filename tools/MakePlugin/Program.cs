// Builds FirePots.esp (ESL-flagged) with Mutagen, then reads it back and checks every record.
//
// Two pot types, each with the same four records:
//   FP_<Type>Pot            MISC  the item you carry, loot and craft
//   FP_<Type>PotProjectile  PROJ  what flies: a missile with gravity that explodes on any contact
//   FP_<Type>PotExplosion   EXPL  the shatter: sounds, decal, damage burst (and for fire, a vanilla
//                                 burning patch left on the ground)
//   FP_<Type>PotBurstEnch   ENCH  the burst's elemental damage (vanilla enchantment effect)
//
// Recipes (cooking pot): ANY empty bottle, jug, pot or urn + ANY filling of the right kind.
// Every container x filling pair is its own recipe, but each recipe only appears when you carry
// that filling and its container is the FIRST one in the container list you carry. So the menu
// shows one row per filling you have (pick which filling to spend), never a wall of duplicates,
// and whichever empty container you have gets used.
//
// Meshes: the plugin leaves item, projectile and explosion models empty; FirePots.dll copies them
// from vanilla forms at load (see [Visuals] in FirePots.ini).
//
// Usage: MakePlugin <output folder>

using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

const string PluginName = "FirePots.esp";

var skyrim = ModKey.FromFileName("Skyrim.esm");
var dawnguard = ModKey.FromFileName("Dawnguard.esm");
var dragonborn = ModKey.FromFileName("Dragonborn.esm");
FormKey Sk(uint id) => new FormKey(skyrim, id);
FormKey Dg(uint id) => new FormKey(dawnguard, id);
FormKey Db(uint id) => new FormKey(dragonborn, id);

// Vanilla forms, IDs from Mutagen.Bethesda.FormKeys.SkyrimSE.
var L_PROJECTILE = Sk(0x088767);
var CraftingCookpot = Sk(0x0A5CB3);

// --- Containers: anything that could hold a filling and shatter. Order = which one gets used
// first when you carry several (cheap clutter first).
var containers = new (string Name, FormKey Key)[]
{
    ("Empty Wine Bottle (01A)", Sk(0x0F2012)),
    ("Empty Wine Bottle (01B)", Sk(0x0F2013)),
    ("Empty Wine Bottle (02A)", Sk(0x0F2014)),
    ("Empty Wine Bottle (02B)", Sk(0x0F2015)),
    ("Empty Spiced Wine Bottle", Sk(0x0FED17)),
    ("Empty Wine Bottle (DG 03)", Dg(0x0093F5)),
    ("Empty Wine Bottle (DG 04)", Dg(0x0093F6)),
    ("Clay Jug (small)", Sk(0x0B9BDE)),
    ("Clay Jug (large)", Sk(0x0B9BD6)),
    ("Nordic Jug (small)", Sk(0x0F08F3)),
    ("Nordic Jug (large)", Sk(0x0F08F1)),
    ("Glazed Jug", Sk(0x08632C)),
    ("Glazed Pot", Sk(0x0B9BD2)),
    ("Glazed Pot (2)", Sk(0x0B9BD4)),
    ("Nordic Pot", Sk(0x0F08F6)),
    ("Nordic Pot (2)", Sk(0x0F08F7)),
    ("Glazed Urn", Sk(0x08632A)),
};

// --- Fillings: (ingredient, how many it takes for one pot)
var fireFillings = new (string Name, FormKey Key, int Count)[]
{
    ("Fire Salts", Sk(0x03AD5E), 1),
    ("Dwarven Oil", Sk(0x0F11C0), 1),
    ("Troll Fat", Sk(0x03AD72), 1),
    ("Charred Skeever Hide", Sk(0x052695), 2),
    ("Spriggan Sap", Sk(0x063B5F), 2),
    ("Ash Creep Cluster", Db(0x01CD74), 2),
    ("Ash Hopper Jelly", Db(0x01CD71), 2),
    ("Burnt Spriggan Wood", Db(0x01CD6E), 2),
    ("Spawn Ash", Db(0x016E26), 2),
};
var frostFillings = new (string Name, FormKey Key, int Count)[]
{
    ("Frost Salts", Sk(0x03AD5F), 1),
    ("Ice Wraith Teeth", Sk(0x03AD6A), 1),
};

// --- Pot types. Fixed form IDs: FirePots.dll looks these up, so they must never move.
var types = new[]
{
    new PotType("Fire", "Fire Pot", 0x800, 15, BurstDamage: 30f,
        DamageEffect: Sk(0x04605A),      // EnchFireDamageFFContact
        DamageKeyword: Sk(0x01CEAD),     // MagicDamageFire
        Sound2: Sk(0x042526),            // TRPOilIgniteLowSD
        ImpactSet: Sk(0x01C2AF),         // MAGFirebolt01ImpactSet (scorch)
        Placed: Sk(0x03E074),            // FireSmLongHazard (burning patch)
        Fillings: fireFillings, FirstRecipeId: 0x900),
    new PotType("Frost", "Frost Pot", 0x810, 20, BurstDamage: 30f,
        DamageEffect: Sk(0x04605B),      // EnchFrostDamageFFContact (also drains stamina)
        DamageKeyword: Sk(0x01CEAE),     // MagicDamageFrost
        Sound2: Sk(0x023992),            // FXExplosionIceSD
        ImpactSet: Sk(0x018A2E),         // MAGFrost01ImpactSet (frost decal)
        Placed: null,
        Fillings: frostFillings, FirstRecipeId: 0xA00),
};
var TRPOilLampBreak = Sk(0x08BBC6);  // shatter sound, both types
const float BlastRadius = 160f;      // units (~2.3 m); the arc preview draws this ring

var outDir = args.Length > 0 ? args[0] : ".";
Directory.CreateDirectory(outDir);
var outPath = Path.Combine(outDir, PluginName);

var modKey = ModKey.FromFileName(PluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.ModHeader.Author = "akeaneschmidt1998";
mod.ModHeader.Description = "Fire Pots: throwable fire and frost pots with wind-up and arc preview (requires FirePots.dll / SKSE).";
mod.IsSmallMaster = true;  // ESL flag: takes no load order slot

ConditionFloat ItemCount(FormKey item, CompareOperator op, float value)
{
    var data = new GetItemCountConditionData { RunOnType = Condition.RunOnType.Subject };
    data.ItemOrList.Link.SetTo(item);
    return new ConditionFloat { CompareOperator = op, ComparisonValue = value, Data = data };
}

ContainerEntry Ingredient(FormKey item, int count)
{
    var ci = new ContainerItem { Count = count };
    ci.Item.SetTo(item);
    return new ContainerEntry { Item = ci };
}

foreach (var t in types)
{
    var burst = new ObjectEffect(new FormKey(modKey, t.BaseId + 3), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotBurstEnch",
        Name = $"{t.Name} Burst",
        Flags = ObjectEffect.Flag.NoAutoCalc,
        CastType = CastType.FireAndForget,
        TargetType = TargetType.Touch,
        EnchantType = ObjectEffect.EnchantTypeEnum.Enchantment,
    };
    var effect = new Effect { Data = new EffectData { Magnitude = t.BurstDamage, Area = 0, Duration = 0 } };
    effect.BaseEffect.SetTo(t.DamageEffect);
    burst.Effects.Add(effect);
    mod.ObjectEffects.Add(burst);

    var explosion = new Explosion(new FormKey(modKey, t.BaseId + 2), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotExplosion",
        Name = t.Name,
        Force = 30f,
        Damage = 5f,
        Radius = BlastRadius,
        ISRadius = 0f,
        VerticalOffsetMult = 0f,
        Flags = 0,
        SoundLevel = SoundLevel.Normal,
    };
    explosion.ObjectEffect.SetTo(burst.FormKey);
    explosion.Sound1.SetTo(TRPOilLampBreak);
    explosion.Sound2.SetTo(t.Sound2);
    explosion.ImpactDataSet.SetTo(t.ImpactSet);
    if (t.Placed is { } placed)
    {
        explosion.PlacedObject.SetTo(placed);
    }
    mod.Explosions.Add(explosion);

    // Missile + Gravity: ballistic arc, explodes on the first thing it touches.
    // Speed is overwritten by FirePots.dll on every throw (wind-up); Gravity 1 = normal gravity.
    var projectile = new Projectile(new FormKey(modKey, t.BaseId + 1), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}PotProjectile",
        Name = t.Name,
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

    var pot = new MiscItem(new FormKey(modKey, t.BaseId), SkyrimRelease.SkyrimSE)
    {
        EditorID = $"FP_{t.Tag}Pot",
        Name = t.Name,
        Value = t.Value,
        Weight = 1.0f,
        Keywords = new() { t.DamageKeyword.ToLink<IKeywordGetter>() },
    };
    mod.MiscItems.Add(pot);

    // Recipes: one per container x filling.
    var id = t.FirstRecipeId;
    foreach (var f in t.Fillings)
    {
        for (var ci = 0; ci < containers.Length; ci++)
        {
            var c = containers[ci];
            var recipe = new ConstructibleObject(new FormKey(modKey, id++), SkyrimRelease.SkyrimSE)
            {
                EditorID = $"FP_Recipe{t.Tag}_{Slug(f.Name)}_{ci:00}",
                CreatedObjectCount = 1,
                Items = new(),
            };
            recipe.CreatedObject.SetTo(pot.FormKey);
            recipe.WorkbenchKeyword.SetTo(CraftingCookpot);
            recipe.Items.Add(Ingredient(c.Key, 1));
            recipe.Items.Add(Ingredient(f.Key, f.Count));
            // shown only with the filling in hand, and this container is the first one carried
            recipe.Conditions.Add(ItemCount(f.Key, CompareOperator.GreaterThanOrEqualTo, 1));
            recipe.Conditions.Add(ItemCount(c.Key, CompareOperator.GreaterThanOrEqualTo, 1));
            for (var earlier = 0; earlier < ci; earlier++)
            {
                recipe.Conditions.Add(ItemCount(containers[earlier].Key, CompareOperator.EqualTo, 0));
            }
            mod.ConstructibleObjects.Add(recipe);
        }
    }
}

// Masters in load order: Skyrim.esm, Dawnguard.esm, Dragonborn.esm (only the referenced ones end up listed).
mod.BeginWrite
    .ToPath(outPath)
    .WithLoadOrder(skyrim, ModKey.FromFileName("Update.esm"), dawnguard, ModKey.FromFileName("HearthFires.esm"), dragonborn)
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
Require(masters.SequenceEqual(new[] { skyrim, dawnguard, dragonborn }), $"masters are Skyrim, Dawnguard, Dragonborn in order (got {string.Join(", ", masters)})");

foreach (var t in types)
{
    var pot = check.MiscItems.Single(m => m.FormKey.ID == t.BaseId);
    Require(pot.Name?.String == t.Name, $"{t.Name} item");

    var p = check.Projectiles.Single(x => x.FormKey.ID == t.BaseId + 1);
    Require(p.Type == Projectile.TypeEnum.Missile && p.Flags.HasFlag(Projectile.Flag.Explosion) && p.Gravity > 0.5f, $"{t.Name} projectile is an exploding missile with gravity");
    Require(p.Explosion.FormKey.ID == t.BaseId + 2, $"{t.Name} projectile -> explosion");
    Require(p.CollisionLayer.FormKey == L_PROJECTILE, $"{t.Name} projectile collision layer");

    var e = check.Explosions.Single(x => x.FormKey.ID == t.BaseId + 2);
    Require(e.ObjectEffect.FormKey.ID == t.BaseId + 3, $"{t.Name} explosion -> burst");
    Require(t.Placed is null ? e.PlacedObject.IsNull : e.PlacedObject.FormKey == t.Placed, $"{t.Name} explosion placed object");
    Require(Math.Abs(e.Radius - BlastRadius) < 0.01f, $"{t.Name} explosion radius");

    var en = check.ObjectEffects.Single(x => x.FormKey.ID == t.BaseId + 3);
    Require(en.Effects.Count == 1 && en.Effects[0].BaseEffect.FormKey == t.DamageEffect, $"{t.Name} burst effect");

    var recipes = check.ConstructibleObjects.Where(r => r.CreatedObject.FormKey == pot.FormKey).ToList();
    Require(recipes.Count == t.Fillings.Length * containers.Length, $"{t.Name}: {t.Fillings.Length} fillings x {containers.Length} containers = {recipes.Count} recipes");
    Require(recipes.All(r => r.WorkbenchKeyword.FormKey == CraftingCookpot), $"{t.Name} recipes are at the cooking pot");
    Require(recipes.All(r => r.Items?.Count == 2), $"{t.Name} recipes use a container and a filling");
    Require(recipes.All(r => r.Conditions.Count >= 2 && r.Conditions.All(c => c.Data is IGetItemCountConditionDataGetter)), $"{t.Name} recipes have item-count conditions");
    // the last container's recipe must rule out every earlier container
    var last = recipes.Last();
    Require(last.Conditions.Count == 2 + containers.Length - 1, $"{t.Name} last recipe excludes all earlier containers");
}

Console.WriteLine($"Wrote and verified {outPath} ({new FileInfo(outPath).Length} bytes): " +
                  $"{types.Length} pot types, {check.ConstructibleObjects.Count} recipes, masters {string.Join(", ", masters)}");

static string Slug(string s) => new string(s.Where(char.IsLetterOrDigit).ToArray());

record PotType(string Tag, string Name, uint BaseId, uint Value, float BurstDamage, FormKey DamageEffect,
    FormKey DamageKeyword, FormKey Sound2, FormKey ImpactSet, FormKey? Placed,
    (string Name, FormKey Key, int Count)[] Fillings, uint FirstRecipeId);
