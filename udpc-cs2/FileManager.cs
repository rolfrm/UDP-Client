namespace udpc_cs2
{
    /// <summary>
    /// This class uses git to have a version controlled list of files.
    /// </summary>
    public class FileManager
    {
        public readonly string Path;

        FileManager(string path)
        {
            this.Path = path;
        }

        public static FileManager Create(string path)
        {
            return new FileManager(path);
        }

        public void Update()
        {
        }
    }
}