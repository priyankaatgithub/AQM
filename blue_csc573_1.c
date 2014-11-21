#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>
#include <net/blue.h>
#include <time.h>

struct blue_csc573_sched_data {
	u8		dscp,
	float	dscp_factor,
};

unsigned long old_time;

static int blue_csc573_enqueue(struct sk_buff *skb, struct Qdisc *sch) {
	struct blue_csc573_sched_data *q = qdisc_priv(sch);
	struct Qdisc *child = q->qdisc;

	q->dscp = ipv4_get_dsfield(ip_hdr(skb)) >> 2;
	switch (q->dscp) {
		case 0x32:
			q->dscp_factor = 0.5;
		case 0x46:
			q->dscp_factor = 0.7;
		case 0x00:
			q->dscp_factor = 0.9;
		default:
			q->dscp_factor = 0.9;
	}

	if(!skb) {
		blue_change_prob(0);
		return 0;
	}	

	switch(blue_action(q->dscp_factor, skb)) {
		case DROP:
			blue_change_prob(1);
			goto congestion_drop;
		case RETAIN:
			printk(KERN_INFO "Packet not marked by BLUE");
			ret = qdisc_enqueue(skb, child);
			if (likely(ret == NET_XMIT_SUCCESS)) {
				sch->q.qlen++;
			} else if (net_xmit_drop_count(ret)) {
				q->stats.pdrop++;
				sch->qstats.drops++;
			}
			return ret;
	}

congestion_drop:
		printk(KERN_INFO "Packet dropped by BLUE");
		qdisc_drop(skb, sch);
		return NET_XMIT_CN;
	
}

static inline void blue_change_prob(int direction){
	if(jiffies - 1 > old_time){
		old_time = jiffies;
		if(direction)
	}
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

	if (child->ops->drop) && (len = child->ops->drop(child)) > 0) {
		q->stats.other++;
		sch->qstats.drops++;
		sch->q.qlen--;
		return len;
	}

	return 0;
}

static void blue_csc573_reset(struct Qdisc *sch){

}

static void blue_csc573_destroy(struct Qdisc *sch){

}

static int blue_csc573_change(struct Qdisc *sch, struct nlattr *opt){

}

static int blue_csc573_init(struct Qdisc *sch, struct nlattr *opt){
	struct blue_csc573_sched_data *q = qdisc_priv(sch);

	old_time = jiffies;

	q->qdisc = &noop_qdisc;
	return blue_csc573_change(sch,opt);
}

static int blue_csc573_dump(struct Qdisc *sch, struct sk_buff *skb){

}

static int blue_csc573_dump_stats(struct Qdisc *sch, struct gnet_dump *d){

}

static int blue_csc573_dump_class(struct Qdisc *sch, unsigned long cl, struct sk_buff *skb, struct tcmsg *tcm){

}

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
	.dump = blue_csc573_dump_class,
};

static Qdisc_ops blue_csc573_qdisc_ops __read_mostly = {
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
	.dump = blue_csc573_dump,
	.dump_stats = blue_csc573_dump_stats,
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