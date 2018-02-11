using System;
using System.Collections.Generic;
using Microsoft.AspNetCore.Mvc;
using Newtonsoft.Json;
namespace Udpc.Share.WebApi.Controllers
{
    public class ConnectNewServer
    {
        public string UserName { get; set; }
    }
    
    [Route("api/[controller]")]
    public class ServerController : Controller
    {
        // GET api/values
        [HttpGet]
        public IEnumerable<ConnectNewServer> Get()
        {
            return new ConnectNewServer[] {};
        }

        // GET api/values/5
        [HttpGet("{id}")]
        public string Get(int id)
        {
            return "value";
        }

        // POST api/values
        [HttpPost]
        public void Post([FromBody] string value)
        {
            var newserver = JsonConvert.DeserializeObject<ConnectNewServer>(value);
        }

        // PUT api/values/5
        [HttpPut("{id}")]
        public void Put(int id, [FromBody] string value)
        {
            throw new NotImplementedException();
        }

        // DELETE api/values/5
        [HttpDelete("{id}")]
        public void Delete(int id)
        {
        }
    }
}