#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>
#include <linux/jiffies.h>

#define DROP 1
#define RETAIN 0

struct blue_csc573_sched_data {
	u8				dscp;
	float			dscp_factor;
	unsigned long	old_time;
	int			probability;
	struct Qdisc 	*qdisc;
};

static inline char blue_action(float dscp_factor, int probability, struct sk_buff *skb){
	int i, bounded_rand;
	get_random_bytes(&i, sizeof(float));
	bounded_rand = i % 100;

	if(bounded_rand < probability*dscp_factor) {
		return DROP;
	}
	else return RETAIN;
}

static inline void blue_change_prob(int direction, struct Qdisc *sch){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);

	if(jiffies - 1 > q->old_time){
		q->old_time = jiffies;
		if(direction) q->probability = q->probability + 1;
		else q->probability = q->probability - 1;
		printk(KERN_INFO "BLUE probability changed to %d\n", q->probability);
	}
}

static int blue_csc573_enqueue(struct sk_buff *skb, struct Qdisc *sch) {
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	struct Qdisc *child = q->qdisc;

	q->dscp = ipv4_get_dsfield(ip_hdr(skb));
	q->dscp = q->dscp & 0xFC;
	switch (q->dscp) {
		case 48:
			q->dscp_factor = 0.5;
		case 68:
			q->dscp_factor = 0.7;
		case 0:
			q->dscp_factor = 0.9;
		default:
			q->dscp_factor = 0.9;
	}

	if(!skb) {
		blue_change_prob(0, sch);
		return 0;
	}	

	switch(blue_action(q->dscp_factor, q->probability, skb)) {
		case DROP:
			blue_change_prob(1, sch);
			goto congestion_drop;
		case RETAIN:
			printk(KERN_INFO "Packet not marked by BLUE");
			int ret = qdisc_enqueue(skb, child);
			if (likely(ret == NET_XMIT_SUCCESS)) {
				sch->q.qlen++;
			} else if (net_xmit_drop_count(ret)) {
				//q->stats.pdrop++;
				sch->qstats.drops++;
			}
			return ret;
	}

congestion_drop:
		printk(KERN_INFO "Packet dropped by BLUE");
		qdisc_drop(skb, sch);
		return NET_XMIT_CN;
	
}

static struct sk_buff *blue_csc573_dequeue(struct Qdisc *sch){
	struct sk_buff *skb;
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	struct Qdisc *child = q->qdisc;

	skb = child->dequeue(child);
	if(skb) {
		qdisc_bstats_update(sch,skb);
		sch->q.qlen--;
	}
	return skb;
}

static struct sk_buff *blue_csc573_peek(struct Qdisc *sch){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	struct Qdisc *child = q->qdisc;

	return child->ops->peek(child);
}

static unsigned int blue_csc573_drop(struct Qdisc *sch){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	struct Qdisc *child = q->qdisc;
	unsigned int len;

	if ((child->ops->drop) && (len = child->ops->drop(child)) > 0) {
		//q->stats.other++;
		sch->qstats.drops++;
		sch->q.qlen--;
		return len;
	}

	return 0;
}

static inline void blue_set_initial(struct blue_csc573_sched_data *q) {
	q->old_time = jiffies;
	q->probability = 10;
}

static void blue_csc573_reset(struct Qdisc *sch){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);

	qdisc_reset(q->qdisc);
	sch->q.qlen = 0;
	blue_set_initial(q);
}

static void blue_csc573_destroy(struct Qdisc *sch){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);

	qdisc_destroy(q->qdisc);
}

static int blue_csc573_change(struct Qdisc *sch, struct nlattr *opt){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	blue_set_initial(q);

	

	return 1;
}

static int blue_csc573_init(struct Qdisc *sch, struct nlattr *opt){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);

	blue_set_initial(q);

	q->qdisc = &noop_qdisc;
	return blue_csc573_change(sch,opt);
}

/*

static int blue_csc573_dump(struct Qdisc *sch, struct sk_buff *skb){

}

static int blue_csc573_dump_stats(struct Qdisc *sch, struct gnet_dump *d){

}

static int blue_csc573_dump_class(struct Qdisc *sch, unsigned long cl, struct sk_buff *skb, struct tcmsg *tcm){

}

*/

static int blue_csc573_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new, struct Qdisc **old){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);

	if (new==NULL) new = &noop_qdisc;

	sch_tree_lock(sch);
	*old = q->qdisc;
	q->qdisc = new;
	qdisc_tree_decrease_qlen(*old, (*old)->q.qlen);
	qdisc_reset(*old);
	sch_tree_unlock(sch);
	return 0;
}

static struct Qdisc *blue_csc573_leaf(struct Qdisc *sch, unsigned long arg){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	return q->qdisc;
}

static unsigned long blue_csc573_get(struct Qdisc *sch, u32 classid){
	return 1;
}

static void blue_csc573_put(struct Qdisc *sch, unsigned long arg){
	// Done. Nothing to add.
}

static void blue_csc573_walk(struct Qdisc *sch, struct qdisc_walker *walker){
	if (!walker->stop) {
		if (walker->count >= walker->skip)
			if (walker->fn(sch, 1, walker) < 0) {
				walker->stop = 1;
				return;
			}
		walker->count++;
	}
}

static const struct Qdisc_class_ops blue_csc573_class_ops = {

	.graft = blue_csc573_graft,
	.leaf = blue_csc573_leaf,
	.get = blue_csc573_get,
	.put = blue_csc573_put,
	.walk = blue_csc573_walk,
	//.dump = blue_csc573_dump_class,
};

static struct Qdisc_ops blue_csc573_qdisc_ops __read_mostly = {
	.id = "blue_csc573",
	.priv_size = sizeof(struct blue_csc573_sched_data),
	.cl_ops = &blue_csc573_class_ops,
	.enqueue = blue_csc573_enqueue,
	.dequeue = blue_csc573_dequeue,
	.peek = blue_csc573_peek,
	.drop = blue_csc573_drop,
	.init = blue_csc573_init,
	.reset = blue_csc573_reset,
	.destroy = blue_csc573_destroy,
	.change = blue_csc573_change,
	//.dump = blue_csc573_dump,
	//.dump_stats = blue_csc573_dump_stats,
	.owner = THIS_MODULE,
};

static int __init blue_csc573_module_init(void){
	return register_qdisc(&blue_csc573_qdisc_ops);
}

static void __exit blue_csc573_module_exit(void){
	unregister_qdisc(&blue_csc573_qdisc_ops);
}

module_init(blue_csc573_module_init)
module_exit(blue_csc573_module_exit)

MODULE_LICENSE("GPL");