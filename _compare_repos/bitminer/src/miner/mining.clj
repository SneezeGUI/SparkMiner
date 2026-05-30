(ns miner.mining
  (:require [digest]
            [clojure.core.async
             :as a
             :refer [>! <! >!! <!! go go-loop chan buffer
                     sliding-buffer close! thread alts! alts!! timeout]]
            [clojure.math.numeric-tower :as math]
            [miner.binascii :refer :all]))


(defn concat-bytes
  "concatenate byte-arrays"
  [& arrays]
  (byte-array (apply concat arrays)))


(defn max-nonce
  "Calculate the upper bound for a given nonce-size"
  [nonce-size]
  (- (math/expt 16 (* nonce-size 2)) 1))


(defn split-nonce-range
  "Splits a given nonce across a range so workers
  need not contest over a single incrementing atom"
  [max-nonce divy-by]
  (range 0 max-nonce (int (/ max-nonce divy-by))))



(defn double-hash [content]
  (-> content
      (digest/sha-256)
      (unhexify)
      (digest/sha-256)))


(defn target-string
  "give the input difficulty number as a non-truncated hex string"
  [big-num]
  (format "%064x" (biginteger big-num)))


(defn calc-difficulty
  "Calculate 'bdiff' difficulty from the Bitcoin
  packed representation"
  [bits]
  (let [base (bit-and bits 0xffffff)
        exponent (bit-shift-right bits 24)
        res (* base (math/expt 2 (* 8 (- exponent 3))))]
    res))


(defn calc-pdiff
  "Calculate Pool ('pdiff') Difficulty relative to the
   maximum possible difficulty"
  [pool-difficulty]
  (let [bdiff-constant 0x1d00ffff
        max-difficulty (calc-difficulty bdiff-constant)
        difference (/ max-difficulty pool-difficulty)]
    (bigint difference)))


(defn calc-pdiff-string
  "Hexadecimal representation of the calculated pdiff value"
  [pool-difficulty]
  (target-string (calc-pdiff pool-difficulty)))


(defn build-merkle-root
  "Recursively hashes the merkle branches with coinbase as the root node"
  [coinbase merkle-branch]
  (reduce
    (fn [root branch]
      (double-hash (concat-bytes (unhexify root)
                                 (unhexify branch))))
    coinbase
    merkle-branch))


(defn build-coinbase
  "Builds the coinbase transaction.
  A concatenation of coinb1, nonce1, nonce2, and coinb2"
  [full-nonce, pool-data]
  (unhexify (str (:coinb1 pool-data)
                 full-nonce
                 (:coinb2 pool-data))))


(defn hash-block
  "Applies Bitcoin's double sha256 to the supplied block-headers"
  [block-headers]
  (double-hash
    (apply concat-bytes (map unhexify (vals block-headers)))))


(defn make-block
  "Builds a block header for the given job + nonce"
  [job-info nonce2]
  (let [full-nonce (str (:nonce job-info) (to-hex nonce2 8))
        coinbase (build-coinbase full-nonce job-info)
        merkle-root (build-merkle-root (hexify coinbase) (:merkle-hashes job-info))]
    (merge
      {:merkle-root merkle-root}
      (select-keys job-info [:version :prev-hash :nonce :nbits :ntime]))))


(defn block-hasher [params nonce]
  (hash-block (make-block params nonce)))


(defn wrap-result [f]
  (fn [& args]
    {:result (apply f args)}))


(defn wrap-bigint [f]
  (fn [& args]
    (BigInteger. (apply f args) 16)))


(defn wrap-metrics
  "HOF for tracking total invocations of the wrapped function"
  [f]
  (let [total-calls (atom 0)]
    (fn [& args]
      (swap! total-calls inc)
      {:result (:result (apply f args))
       :total-calls @total-calls})))


(defn build-submit-payload
  [job nonce]
  [(str (:jobid job))
   (str (to-hex nonce 8))
   (str (:ntime job))
   (str (:nonce job))])


(defn make-worker
  [hash-func]
  (fn [nonce2-base]
    (let [pid (.toString (java.util.UUID/randomUUID))
          ; primary channel from the outside world to receive new work
          input-ch (chan)
          ; allow a little back-pressure before parking
          ; so we can keep calculating hashes while IO happens
          result-ch (chan 5)
          ; sliding stats buffer (hash/sec, runtime, etc..)
          stats-ch (chan (sliding-buffer 10))

          job (atom {})
          nonce2 (atom nonce2-base)
          pdiff (atom Long/MAX_VALUE)
          running (atom true)]

      ; a separate go block to listen for new job info
      ; coming in from the outside world
      (go-loop [msg {}]
        (when (contains? msg :new-job)
          (reset! job (:new-job msg))
          (reset! nonce2 nonce2-base)
          (reset! pdiff (calc-pdiff (get-in msg [:new-job :difficulty]))))
        (recur (<! input-ch)))

      ; the main consumer loop which repeatedly applies
      ; the hash function to the data
      (go-loop []
        (when (and @running (not (empty? @job)))
          (let [result (hash-func @job @nonce2)]
            (when (< (:result result) @pdiff)
              (println "Holy crap!!")
              (>! result-ch (build-submit-payload @job @nonce2)))
            (swap! nonce2 inc)
            (>! stats-ch {:total-calls (:total-calls  result)})))
        (recur))
      [pid input-ch result-ch stats-ch])))










