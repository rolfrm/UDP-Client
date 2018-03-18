namespace Udpc.Share.DataLog
{
    public struct DataLogFilePosition
    {
        public long DataPos;
        public long CommitPos;
        public long CommitsCount;
    }
}