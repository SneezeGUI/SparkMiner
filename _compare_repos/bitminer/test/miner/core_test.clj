(ns miner.core-test
  (:require [clojure.test :refer :all]
            [miner.core :refer :all])
  (:require miner.core))


; retrieved via bitcoin-cli
(def block3 {:prev-hash "000000006a625f06636b8bb6ac7b960a8d03705d1ace08b1a19da3fdcc99ddbd"
             :merkle-root "999e1c837c76a1b7fbb7e57baf87b309960f5ffefbf2a9b95dd890602272f644"
             :version 1
             :nbits "1d00ffff"
             :nonce 1844305925
             :ntime 1231470173})


(deftest block-hashing
  (testing "confirming that we can calculate a block hash
            from a set of known headers"
    (let [expected "0000000082b5015589a3fdf2d4baff403e6f0be035a5d9742c1cae6295464449"
          data (str
                 (swap-endianness (to-hex (:version block3) 8))
                 (swap-endianness (:prev-hash block3))
                 (swap-endianness (:merkle-root block3))
                 (swap-endianness (format "%02x" (:ntime block3)))
                 (swap-endianness (:nbits block3))
                 (swap-endianness (format "%02x" (:nonce block3))))
          result (swap-endianness (double-hash (unhexify data)))]
      (println result)
      (is (= expected result)))))

(deftest difficulty-calculation
  (testing "that our difficulty function matches the examples
            from en.bitcoin.it/wiki/Difficulty"
    (let [input 0x19015f53
          expected "00000000000000015f5300000000000000000000000000000000000000000000"
          result (target-string (calc-difficulty input))]
      (is (= expected result)))))