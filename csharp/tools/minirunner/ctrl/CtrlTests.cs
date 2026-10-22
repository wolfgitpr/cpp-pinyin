using Xunit;

public class ControlTests
{
    [Fact]
    public void ThisOnePasses()
    {
        Assert.Equal(2, 1 + 1);
    }

    [Fact]
    public void ThisOneFails()
    {
        Assert.Equal(3, 1 + 1);
    }
}
