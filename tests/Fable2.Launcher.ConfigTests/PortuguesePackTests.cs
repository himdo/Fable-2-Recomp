using System.IO.Compression;
using System.Text;
using Fable2Launcher;

internal static class PortuguesePackTests
{
    public static void Run(string root)
    {
        string data = Path.Combine(root, "data");
        string cultures = Path.Combine(data, "language/cultures.txt");
        string manifest = Path.Combine(data, "dir.manifest");
        string book = Path.Combine(data, "language/pt-br/text/book.babel");
        string speech = Path.Combine(data, "language/pt-br/speechdoc/speech.adb");
        string[] games = ["common", "keystone", "magedice", "wizardstower"];
        const string catalogue = "; fallback\nen-uk\tNAME_EN\tnull\nxx\tMY_MOD\ten-uk\n" +
            "; The following are the languages supported by the game\nen-us\tNAME_US\ten-uk\n" +
            "pt-br\tNAME_BR\ten   ; edited by another mod\npt-pt\tNAME_PT\ten";
        foreach (var encoding in new[] { Encoding.BigEndianUnicode, Encoding.Unicode })
        {
            foreach (string newline in new[] { "\n", "\r\n" })
            {
                string original = catalogue.Replace("\n", newline);
                byte[] bytes = [.. encoding.GetPreamble(), .. encoding.GetBytes(original)];
                byte[] edited = BrazilianPortuguesePack.MergeCultures(bytes);
                string expected = original.Replace("NAME_EN\tnull", "NAME_EN\tnull" + newline + "pt-br\tGUI_SCREEN_OPTIONS_LANGUAGE_PT_BR\ten-uk")
                    .Replace("NAME_BR\ten ", "NAME_BR\tpt-br ");
                Check(edited.SequenceEqual(encoding.GetPreamble().Concat(encoding.GetBytes(expected))));
                Check(edited.SequenceEqual(BrazilianPortuguesePack.MergeCultures(edited)));
            }
        }
        Write(Path.Combine(root, "default.xex"), "test");
        Write(Path.Combine(data, "language/en-uk/text/book.babel"), "en");
        Write(Path.Combine(data, "language/en-uk/speech/speech.bnk"), "original audio");
        File.WriteAllText(cultures, catalogue, Encoding.BigEndianUnicode);
        byte[] originalCultures = File.ReadAllBytes(cultures);
        const string originalManifest = "language\\en-uk\\text\\book.babel\r\n";
        Write(manifest, originalManifest);
        foreach (string game in games) Write(Path.Combine(data, $"pubgames/{game}/localization.pt.xml"), "original " + game);

        string zipPath = Path.Combine(root, "pack.zip");
        void MakeZip(bool complete)
        {
            using var zip = new ZipArchive(File.Create(zipPath), ZipArchiveMode.Create);
            void Add(string name)
            {
                using var stream = new StreamWriter(zip.CreateEntry("beta/" + name).Open());
                stream.Write("pack " + name);
            }
            Add("language/pt-br/text/book.babel");
            if (complete) Add("language/pt-br/speechdoc/speech.adb");
            Add("../unexpected-file");
            Add("language/pt-br/speechdoc/extracted/dev.bak");
            foreach (string game in games) Add($"pubgames/{game}/localization.pt.xml");
        }
        MakeZip(false);
        try { BrazilianPortuguesePack.Install(root, zipPath); throw new Exception("Incomplete ZIP accepted."); }
        catch (InvalidDataException) { }
        Check(!File.Exists(book) && !BrazilianPortuguesePack.IsInstalled(root));

        MakeZip(true);
        // The path to the second file is blocked after the first has been written.
        Directory.CreateDirectory(speech);
        try { BrazilianPortuguesePack.Install(root, zipPath); throw new Exception("Blocked install succeeded."); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        Check(!File.Exists(book) && !BrazilianPortuguesePack.IsInstalled(root));
        Check(File.ReadAllBytes(cultures).SequenceEqual(originalCultures));
        Directory.Delete(speech);
        Directory.Delete(Path.GetDirectoryName(speech)!);
        Directory.Delete(Path.Combine(data, "language/pt-br"));
        BrazilianPortuguesePack.Install(root, zipPath);
        Check(BrazilianPortuguesePack.IsInstalled(root));
        Check(File.ReadAllText(book).StartsWith("pack ") && File.ReadAllText(speech).StartsWith("pack "));
        Check(!Directory.Exists(Path.Combine(data, "language/pt-br/speechdoc/extracted")));
        Check(!File.Exists(Path.Combine(root, "unexpected-file")));
        byte[] installed = File.ReadAllBytes(book);
        File.WriteAllText(book, "newer mod");
        try { BrazilianPortuguesePack.Remove(root); throw new Exception("Conflicting edit was overwritten."); }
        catch (IOException) { }
        Check(BrazilianPortuguesePack.IsInstalled(root) && File.ReadAllText(book) == "newer mod");
        File.WriteAllBytes(book, installed);
        // Simulate the healer indexing the pack, another mod, and an interrupted restore.
        File.AppendAllText(manifest, "Language/PT-BR/Text/book.babel\r\nlanguage\\pt-br\\speechdoc\\speech.adb\r\nother\\mod\r\n");
        File.Delete(speech);
        BrazilianPortuguesePack.Remove(root);
        BrazilianPortuguesePack.Remove(root);
        Check(!BrazilianPortuguesePack.IsInstalled(root) && !Directory.Exists(Path.Combine(data, "language/pt-br")));
        Check(originalCultures.SequenceEqual(File.ReadAllBytes(cultures)));
        Check(File.ReadAllText(manifest) == originalManifest + "other\\mod\r\n");
        foreach (string game in games) Check(File.ReadAllText(Path.Combine(data, $"pubgames/{game}/localization.pt.xml")) == "original " + game);

        // A manually installed translation must be restored by removal as well.
        Write(book, "previous translation");
        BrazilianPortuguesePack.Install(root, zipPath);
        BrazilianPortuguesePack.Remove(root);
        Check(File.ReadAllText(book) == "previous translation");
        Console.WriteLine("PT-BR installation, conflict detection, recovery and removal passed.");
    }

    private static void Write(string path, string contents)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, contents);
    }

    private static void Check(bool ok)
    {
        if (!ok) throw new InvalidOperationException("PT-BR pack regression test failed.");
    }
}
