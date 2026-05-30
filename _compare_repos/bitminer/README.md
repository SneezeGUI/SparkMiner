# bitminer

![8632995866_b36d64cc23_h](https://user-images.githubusercontent.com/1408720/31059490-fa24c79a-a6b7-11e7-977a-6f703c5b4b2e.jpg)

### About 

Want a miner that runs at a hash rate of just 1.0% that of a modern ASIC miner? Well this is not that miner. This is a miner that runs at a hash rate of _less than_ 0.1% that of a modern ASIC miner[0]!

### Why

Learning. I wanted to peel back the covers on Bitcoin and pool based mining, and also try my hand at a larger Clojure project along the way. 

## Core features: 
 - [X] Clojure (so pretty cool) 
 - [X] Slow 
 - [X] Costs more to run than it will ever make back in bitcoin
 - [X] Includes 2 pretty solid tests
 - [X] Peeled back the covers a bit on how Bitcoin works

### Intallation & Operation

Wanna run it?  

    git clone https://github.com/chriskiehl/bitminer.git 
    
Leningen should be used to run the repl and resolve the deps. 

    cd bitminer/
    lein repl 
    > (start-supervisor) 


Then it's a waiting game. Shortly before the heat death of the universe you should be able to nab a share.


[0] Based on miners (as of this 2017) operating in the TH/s range, and my current i7, being run inside of a vm, using only a few cores, being in the KH/s range. 
